#include <chrono>
#include <random>
#include <cmath>
#include <fstream>
#include <algorithm>
#include <array>
#include <initializer_list>
#include <vector>
#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>

#include "Headers/DrawText.hpp"
#include "Headers/Global.hpp"
#include "Headers/GetTetromino.hpp"
#include "Headers/GetWallKickData.hpp"
#include "Headers/Tetromino.hpp"

int main()
{
	enum class GameState { Menu, HighScores, Help, Playing, Paused, GameOver };
	GameState state = GameState::Menu;

	std::vector<unsigned> high_scores(10, 0);
	auto load_high_scores = [&]() {
		std::ifstream in("highscores.txt");
		if (!in.is_open()) return;
		for (unsigned i = 0; i < high_scores.size() && in; ++i) in >> high_scores[i];
	};
	auto save_high_scores = [&]() {
		std::ofstream out("highscores.txt", std::ios::trunc);
		for (unsigned i = 0; i < high_scores.size(); ++i)
		{
			out << high_scores[i];
			if (i + 1 != high_scores.size()) out << ' ';
		}
	};
	load_high_scores();

	bool game_over = false;
	bool hard_drop_pressed = false;
	bool rotate_pressed = false;

	unsigned lag = 0;
	unsigned lines_cleared = 0;

	unsigned char clear_effect_timer = 0;
	unsigned char current_fall_speed = START_FALL_SPEED;
	unsigned char fall_timer = 0;
	unsigned char move_timer = 0;
	unsigned char next_shape;
	unsigned char soft_drop_timer = 0;

	std::chrono::time_point<std::chrono::steady_clock> previous_time;

	unsigned score = 0;
	unsigned level = 1;
	bool advanced_mode = false;
	unsigned locked_rows = 0;
	const std::chrono::microseconds difficulty_interval(5 * 60 * 1000000ll);
	std::chrono::microseconds accumulated_play_time(0);
	bool score_posted = false;

	std::random_device random_device;

	std::default_random_engine random_engine(random_device());

	std::uniform_int_distribution<unsigned short> shape_distribution(0, 6);

	std::vector<bool> clear_lines(ROWS, false);

	std::vector<sf::Color> cell_colors = {
		sf::Color(36, 36, 85),
		sf::Color(0, 219, 255),
		sf::Color(0, 36, 255),
		sf::Color(255, 146, 0),
		sf::Color(255, 219, 0),
		sf::Color(0, 219, 0),
		sf::Color(146, 0, 255),
		sf::Color(219, 0, 0),
		sf::Color(73, 73, 85)
	};

	std::vector<std::vector<unsigned char>> matrix(COLUMNS, std::vector<unsigned char>(ROWS));

	sf::RenderWindow window(
		sf::VideoMode({static_cast<unsigned int>(2 * CELL_SIZE * COLUMNS * SCREEN_RESIZE),
					   static_cast<unsigned int>(CELL_SIZE * ROWS * SCREEN_RESIZE)}),
		"Tetris",
		sf::Style::Close);

	sf::FloatRect view_rect{sf::Vector2f{0.f, 0.f}, sf::Vector2f{static_cast<float>(2 * CELL_SIZE * COLUMNS), static_cast<float>(CELL_SIZE * ROWS)}};
	window.setView(sf::View(view_rect));

	auto load_texture = [](sf::Texture& tex, std::initializer_list<std::string> paths) {
		for (const auto& p : paths)
		{
			if (tex.loadFromFile(p)) return true;
		}
		return false;
	};

	sf::Texture tex_background;
	sf::Texture tex_frame;
	sf::Texture tex_scorebar;
	sf::Texture tex_nextbox;

	bool has_background = load_texture(tex_background, {
		"Resources/Images/background.png",
		"C:/Users/M.Ahad Ali/Desktop/Tetris Project/Resources/Images/background.png"
	});
	bool has_frame = load_texture(tex_frame, {"Resources/Images/frame.png", "Project/img/frame.png"});
	bool has_scorebar = load_texture(tex_scorebar, {"Resources/Images/Score bar.png", "Project/img/Score bar.png"});
	bool has_nextbox = load_texture(tex_nextbox, {"Resources/Images/Next tetriminos shown.png", "Project/img/Next tetriminos shown.png"});

	sf::Sprite background_sprite(tex_background);
	sf::Sprite frame_sprite(tex_frame);
	sf::Sprite scorebar_sprite(tex_scorebar);
	sf::Sprite nextbox_sprite(tex_nextbox);

	if (has_background)
	{
		background_sprite.setScale(sf::Vector2f(view_rect.size.x / tex_background.getSize().x, view_rect.size.y / tex_background.getSize().y));
		background_sprite.setPosition(sf::Vector2f(0.f, 0.f));
	}
	if (has_frame)
	{
		float target_w = static_cast<float>(CELL_SIZE * COLUMNS + 4);
		float target_h = static_cast<float>(CELL_SIZE * ROWS + 4);
		frame_sprite.setScale(sf::Vector2f(target_w / static_cast<float>(tex_frame.getSize().x), target_h / static_cast<float>(tex_frame.getSize().y)));
		frame_sprite.setPosition(sf::Vector2f(-2.f, -2.f));
	}
	if (has_scorebar)
	{
		scorebar_sprite.setScale(sf::Vector2f((CELL_SIZE * (COLUMNS - 1)) / static_cast<float>(tex_scorebar.getSize().x), (CELL_SIZE * 5) / static_cast<float>(tex_scorebar.getSize().y)));
	}
	if (has_nextbox)
	{
		nextbox_sprite.setScale(sf::Vector2f((CELL_SIZE * 5) / static_cast<float>(tex_nextbox.getSize().x), (CELL_SIZE * 5) / static_cast<float>(tex_nextbox.getSize().y)));
	}

	auto generate_shape = [&]() -> unsigned char {
		if (advanced_mode)
		{
			return static_cast<unsigned char>(shape_distribution(random_engine));
		}
		std::uniform_int_distribution<int> basic_dist(0, 3);
		return static_cast<unsigned char>(basic_dist(random_engine));
	};

	Tetromino tetromino(generate_shape(), matrix);

	next_shape = generate_shape();

	auto fill_locked_rows = [&]() {
		for (unsigned char row = 0; row < locked_rows; ++row)
		{
			unsigned char target = static_cast<unsigned char>(ROWS - 1 - row);
			for (unsigned char col = 0; col < COLUMNS; ++col)
			{
				matrix[col][target] = 8;
			}
		}
	};

	auto reset_game = [&](bool adv) {
		advanced_mode = adv;
		score = 0;
		lines_cleared = 0;
		level = adv ? 2 : 1;
		current_fall_speed = static_cast<unsigned char>(std::max<int>(SOFT_DROP_SPEED, START_FALL_SPEED - (level - 1)));
		fall_timer = 0;
		move_timer = 0;
		soft_drop_timer = 0;
		clear_effect_timer = 0;
		game_over = false;
		score_posted = false;
		locked_rows = 0;
		for (auto& col : matrix) std::fill(col.begin(), col.end(), 0);
		std::fill(clear_lines.begin(), clear_lines.end(), 0);
		fill_locked_rows();
		tetromino = Tetromino(generate_shape(), matrix);
		next_shape = generate_shape();
		accumulated_play_time = std::chrono::microseconds(0);
	};

	auto update_level_speed = [&]() {
		level = (advanced_mode ? 2u : 1u) + lines_cleared / 10;
		current_fall_speed = static_cast<unsigned char>(std::max<int>(SOFT_DROP_SPEED, START_FALL_SPEED - static_cast<int>(level - 1)));
	};

	auto try_post_score = [&]() {
		if (score_posted) return;
		high_scores.push_back(score);
		std::sort(high_scores.begin(), high_scores.end(), std::greater<unsigned>());
		if (high_scores.size() > 10) high_scores.resize(10);
		save_high_scores();
		score_posted = true;
	};

	previous_time = std::chrono::steady_clock::now();

	while (window.isOpen())
	{
		unsigned delta_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - previous_time).count();

		lag += delta_time;

		previous_time += std::chrono::microseconds(delta_time);

		while (FRAME_DURATION <= lag)
		{
			lag -= FRAME_DURATION;

			while (auto ev = window.pollEvent())
			{
				if (ev->is<sf::Event::Closed>())
				{
					window.close();
				}
				else if (auto keyRel = ev->getIf<sf::Event::KeyReleased>())
				{
					switch (state)
					{
						case GameState::Playing:
						case GameState::Paused:
						{
							if (keyRel->scancode == sf::Keyboard::Scancode::P)
							{
								state = (state == GameState::Playing) ? GameState::Paused : GameState::Playing;
								break;
							}
							if (keyRel->scancode == sf::Keyboard::Scancode::Enter)
							{
								state = GameState::Menu;
								break;
							}

							if (state == GameState::Paused)
							{
								switch (keyRel->scancode)
								{
									case sf::Keyboard::Scancode::Num1:
										reset_game(false);
										state = GameState::Playing;
										break;
									case sf::Keyboard::Scancode::Num2:
										reset_game(true);
										state = GameState::Playing;
										break;
									case sf::Keyboard::Scancode::Num3:
										state = GameState::HighScores;
										break;
									case sf::Keyboard::Scancode::Num4:
										state = GameState::Help;
										break;
									case sf::Keyboard::Scancode::Num5:
										state = GameState::Playing;
										break;
									default:
										break;
								}
								break;
							}

							// gameplay key releases
							switch (keyRel->scancode)
							{
								case sf::Keyboard::Scancode::Z:
								case sf::Keyboard::Scancode::C:
									rotate_pressed = 0;
									break;
								case sf::Keyboard::Scancode::Down:
									soft_drop_timer = 0;
									break;
								case sf::Keyboard::Scancode::Left:
								case sf::Keyboard::Scancode::Right:
									move_timer = 0;
									break;
								case sf::Keyboard::Scancode::Space:
									hard_drop_pressed = 0;
									break;
								default:
									break;
							}
							break;
						}
						case GameState::GameOver:
						{
							if (keyRel->scancode == sf::Keyboard::Scancode::Enter)
							{
								state = GameState::Menu;
							}
							break;
						}
						case GameState::Menu:
						{
							switch (keyRel->scancode)
							{
								case sf::Keyboard::Scancode::Num1:
									reset_game(false);
									state = GameState::Playing;
									break;
								case sf::Keyboard::Scancode::Num2:
									reset_game(true);
									state = GameState::Playing;
									break;
								case sf::Keyboard::Scancode::Num3:
									state = GameState::HighScores;
									break;
								case sf::Keyboard::Scancode::Num4:
									state = GameState::Help;
									break;
								case sf::Keyboard::Scancode::Num5:
									window.close();
									break;
								default:
									break;
							}
							break;
						}
						case GameState::HighScores:
						case GameState::Help:
						{
							// any key to return to menu
							state = GameState::Menu;
							break;
						}
					}
				}
			}

		
			if (state == GameState::Playing)
			{
				accumulated_play_time += std::chrono::microseconds(FRAME_DURATION);
				if (accumulated_play_time >= difficulty_interval * (locked_rows + 1) && locked_rows + 1 < ROWS)
				{
					locked_rows++;
					fill_locked_rows();
				}

				if (0 == clear_effect_timer)
				{
					if (0 == game_over)
					{
						if (0 == rotate_pressed)
						{
							if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::Z))
							{
								rotate_pressed = 1;
								tetromino.rotate(false, matrix);
							}
							else if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::C))
							{
								rotate_pressed = 1;
								tetromino.rotate(true, matrix);
							}
						}

						if (0 == move_timer)
						{
							if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::Left))
							{
								move_timer = 1;
								tetromino.move_left(matrix);
							}
							else if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::Right))
							{
								move_timer = 1;
								tetromino.move_right(matrix);
							}
						}
						else
						{
							move_timer = static_cast<unsigned char>((1 + move_timer) % MOVE_SPEED);
						}

						if (0 == hard_drop_pressed && sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::Space))
						{
							hard_drop_pressed = 1;
							fall_timer = current_fall_speed;
							tetromino.hard_drop(matrix);
						}

						if (0 == soft_drop_timer)
						{
							if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::Down))
							{
								if (tetromino.move_down(matrix))
								{
									fall_timer = 0;
									soft_drop_timer = 1;
								}
							}
						}
						else
						{
							soft_drop_timer = static_cast<unsigned char>((1 + soft_drop_timer) % SOFT_DROP_SPEED);
						}

						if (current_fall_speed == fall_timer)
						{
							if (!tetromino.move_down(matrix))
							{
								tetromino.update_matrix(matrix);

								unsigned cleared_now = 0;
								unsigned mono_cleared = 0;
								for (unsigned char a = 0; a < ROWS; a++)
								{
									if (a >= ROWS - locked_rows) continue;
									bool clear_line = true;
									bool mono_line = true;
									unsigned char first_color = 0;
									for (unsigned char b = 0; b < COLUMNS; b++)
									{
										if (0 == matrix[b][a])
										{
											clear_line = false;
											break;
										}

										if (first_color == 0)
										{
											first_color = matrix[b][a];
										}
										else if (matrix[b][a] != first_color)
										{
											mono_line = false;
										}
									}

									if (clear_line)
									{
										lines_cleared++;
										cleared_now++;
										if (mono_line) mono_cleared++;
										clear_effect_timer = CLEAR_EFFECT_DURATION;
										clear_lines[a] = true;
									}
								}

								if (cleared_now)
								{
									static const unsigned score_table[4] = {10, 30, 60, 100};
									unsigned base = score_table[std::min<unsigned>(cleared_now, 4) - 1];
									unsigned mono_bonus = 20 * mono_cleared;
									score += (base + mono_bonus) * level;
									update_level_speed();
								}

								if (0 == clear_effect_timer)
								{
									game_over = (0 == tetromino.reset(next_shape, matrix));
									next_shape = generate_shape();
									if (game_over)
									{
										state = GameState::GameOver;
										try_post_score();
									}
								}
							}

							fall_timer = 0;
						}
						else
						{
							fall_timer++;
						}
					}
				}
				else
				{
					clear_effect_timer--;
					if (0 == clear_effect_timer)
					{
						for (unsigned char a = 0; a < ROWS; a++)
						{
							if (1 == clear_lines[a])
							{
								for (unsigned char b = 0; b < COLUMNS; b++)
								{
									matrix[b][a] = 0;
									for (unsigned char c = a; c > 0; c--)
									{
										matrix[b][c] = matrix[b][c - 1];
										matrix[b][c - 1] = 0;
									}
								}
							}
						}

						game_over = 0 == tetromino.reset(next_shape, matrix);
						next_shape = generate_shape();
						std::fill(clear_lines.begin(), clear_lines.end(), 0);
					}
				}
			}

			//Here we're drawing everything!
			if (FRAME_DURATION > lag)
			{
				window.clear();

				unsigned short center_x = static_cast<unsigned short>(0.5f * CELL_SIZE * COLUMNS);
				unsigned short center_y = static_cast<unsigned short>(0.5f * CELL_SIZE * ROWS);
				unsigned short modal_w = static_cast<unsigned short>(CELL_SIZE * COLUMNS);
				unsigned short modal_h = static_cast<unsigned short>(CELL_SIZE * ((ROWS / 2) + 1));
				unsigned short modal_x = static_cast<unsigned short>(0.5f * CELL_SIZE * COLUMNS - 0.5f * modal_w);
				unsigned short modal_y = static_cast<unsigned short>(0.5f * CELL_SIZE * ROWS - 0.5f * modal_h);

				unsigned total_seconds = static_cast<unsigned>(accumulated_play_time.count() / 1000000);
				unsigned minutes = total_seconds / 60;
				unsigned seconds = total_seconds % 60;
				std::string time_text = std::to_string(minutes) + ":" + (seconds < 10 ? std::string("0") : std::string("")) + std::to_string(seconds);

				unsigned char clear_cell_size = static_cast<unsigned char>(2 * std::round(0.5f * CELL_SIZE * (clear_effect_timer / static_cast<float>(CLEAR_EFFECT_DURATION))));

				sf::RectangleShape cell(sf::Vector2f(static_cast<float>(CELL_SIZE - 1), static_cast<float>(CELL_SIZE - 1)));
				sf::RectangleShape playfield_border(sf::Vector2f(static_cast<float>(CELL_SIZE * COLUMNS), static_cast<float>(CELL_SIZE * ROWS)));
				playfield_border.setPosition(sf::Vector2f(0.f, 0.f));
				playfield_border.setFillColor(sf::Color(18, 18, 28));
				playfield_border.setOutlineThickness(2.f);
				playfield_border.setOutlineColor(sf::Color(80, 80, 130));

				float side_x = static_cast<float>(CELL_SIZE * (COLUMNS + 0.05f));
				float side_y = 4.f;
				float side_w = static_cast<float>(CELL_SIZE * (COLUMNS - 0.25f));
				float side_h = static_cast<float>(CELL_SIZE * ROWS - 8.f);

				sf::RectangleShape side_panel(sf::Vector2f(side_w, side_h));
				side_panel.setPosition(sf::Vector2f(side_x, side_y));
				side_panel.setFillColor(sf::Color(12, 12, 20, 230));
				side_panel.setOutlineThickness(2.f);
				side_panel.setOutlineColor(sf::Color(70, 70, 110));

				float next_block_h = static_cast<float>(4 * CELL_SIZE + 14);
				sf::RectangleShape next_panel(sf::Vector2f(side_w - 8.f, next_block_h));
				next_panel.setPosition(sf::Vector2f(side_x + 4.f, side_y + 4.f));
				next_panel.setFillColor(sf::Color(8, 8, 14, 230));
				next_panel.setOutlineThickness(1.5f);
				next_panel.setOutlineColor(sf::Color(90, 90, 140));

				sf::RectangleShape stats_panel(sf::Vector2f(side_w - 8.f, side_h - next_block_h - 10.f));
				stats_panel.setPosition(sf::Vector2f(side_x + 4.f, side_y + next_block_h + 6.f));
				stats_panel.setFillColor(sf::Color(10, 10, 16, 230));
				stats_panel.setOutlineThickness(1.5f);
				stats_panel.setOutlineColor(sf::Color(70, 70, 110));

				sf::RectangleShape preview_border(sf::Vector2f(static_cast<float>(5 * CELL_SIZE), static_cast<float>(4 * CELL_SIZE)));
				preview_border.setFillColor(sf::Color(6, 6, 12));
				preview_border.setOutlineThickness(1.f);
				preview_border.setOutlineColor(sf::Color(90, 90, 140));
				preview_border.setPosition(sf::Vector2f(next_panel.getPosition().x + 10.f, next_panel.getPosition().y + 16.f));

				sf::RectangleShape modal_shadow(sf::Vector2f(static_cast<float>(modal_w + 12), static_cast<float>(modal_h + 12)));
				modal_shadow.setPosition(sf::Vector2f(static_cast<float>(modal_x - 6), static_cast<float>(modal_y - 6)));
				modal_shadow.setFillColor(sf::Color(0, 0, 0, 170));

				sf::RectangleShape modal_back(sf::Vector2f(static_cast<float>(modal_w), static_cast<float>(modal_h)));
				modal_back.setPosition(sf::Vector2f(static_cast<float>(modal_x), static_cast<float>(modal_y)));
				modal_back.setFillColor(sf::Color(16, 18, 30, 235));
				modal_back.setOutlineThickness(2.5f);
				modal_back.setOutlineColor(sf::Color(90, 200, 255));

				auto draw_playfield = [&](bool draw_active_piece, bool show_background, bool draw_ui) {
					if (show_background && has_background)
					{
						window.draw(background_sprite);
					}
					else
					{
						sf::RectangleShape backdrop(sf::Vector2f(view_rect.size.x, view_rect.size.y));
						backdrop.setFillColor(sf::Color(8, 10, 18));
						window.draw(backdrop);
					}

					if (!draw_ui)
					{
						return;
					}

					// vignette overlay for depth
					window.draw(playfield_border);
					if (has_frame)
					{
						frame_sprite.setPosition(sf::Vector2f(0.f, 0.f));
						window.draw(frame_sprite);
					}
					window.draw(side_panel);
					window.draw(next_panel);
					window.draw(preview_border);
					window.draw(stats_panel);
					//Draw the matrix
					for (unsigned char a = 0; a < COLUMNS; a++)
					{
						for (unsigned char b = 0; b < ROWS; b++)
						{
							if (0 == clear_lines[b])
							{
								cell.setPosition(sf::Vector2f(static_cast<float>(CELL_SIZE * a), static_cast<float>(CELL_SIZE * b)));
								cell.setFillColor(cell_colors[matrix[a][b]]);
								window.draw(cell);
							}
						}
					}

					//Ghost + active tetromino
					if (draw_active_piece && 0 == game_over)
					{
						cell.setFillColor(cell_colors[8]);
						for (Position& mino : tetromino.get_ghost_minos(matrix))
						{
							cell.setPosition(sf::Vector2f(static_cast<float>(CELL_SIZE * mino.x), static_cast<float>(CELL_SIZE * mino.y)));
							window.draw(cell);
						}

						cell.setFillColor(cell_colors[1 + tetromino.get_shape()]);
						for (Position& mino : tetromino.get_minos())
						{
							cell.setPosition(sf::Vector2f(static_cast<float>(CELL_SIZE * mino.x), static_cast<float>(CELL_SIZE * mino.y)));
							window.draw(cell);
						}
					}

					//Clear effect overlay
					for (unsigned char a = 0; a < COLUMNS; a++)
					{
						for (unsigned char b = 0; b < ROWS; b++)
						{
							if (1 == clear_lines[b])
							{
								cell.setFillColor(cell_colors[0]);
								cell.setPosition(sf::Vector2f(static_cast<float>(CELL_SIZE * a), static_cast<float>(CELL_SIZE * b)));
								cell.setSize(sf::Vector2f(static_cast<float>(CELL_SIZE - 1), static_cast<float>(CELL_SIZE - 1)));
								window.draw(cell);

								cell.setFillColor(sf::Color(255, 255, 255));
								cell.setPosition(sf::Vector2f(static_cast<float>(std::floor(CELL_SIZE * (0.5f + a) - 0.5f * clear_cell_size)), static_cast<float>(std::floor(CELL_SIZE * (0.5f + b) - 0.5f * clear_cell_size))));
								cell.setSize(sf::Vector2f(static_cast<float>(clear_cell_size), static_cast<float>(clear_cell_size)));
								window.draw(cell);
							}
						}
					}

					if (draw_active_piece)
					{
						cell.setFillColor(cell_colors[1 + next_shape]);
						cell.setSize(sf::Vector2f(static_cast<float>(CELL_SIZE - 1), static_cast<float>(CELL_SIZE - 1)));
						if (has_nextbox)
						{
							nextbox_sprite.setPosition(preview_border.getPosition());
							window.draw(nextbox_sprite);
						}
						else
						{
							window.draw(preview_border);
						}

						float base_x = preview_border.getPosition().x;
						float base_y = preview_border.getPosition().y;
						auto preview_minos = get_tetromino(next_shape, 1, 1);
						char min_x = preview_minos[0].x, max_x = preview_minos[0].x;
						char min_y = preview_minos[0].y, max_y = preview_minos[0].y;
						for (const auto& m : preview_minos)
						{
							min_x = std::min(min_x, m.x);
							max_x = std::max(max_x, m.x);
							min_y = std::min(min_y, m.y);
							max_y = std::max(max_y, m.y);
						}
						float shape_w = static_cast<float>((max_x - min_x + 1) * CELL_SIZE);
						float shape_h = static_cast<float>((max_y - min_y + 1) * CELL_SIZE);
						float offset_x = base_x + 0.5f * (preview_border.getSize().x - shape_w) - static_cast<float>(min_x * CELL_SIZE);
						float offset_y = base_y + 0.5f * (preview_border.getSize().y - shape_h) - static_cast<float>(min_y * CELL_SIZE);
						for (const auto& mino : preview_minos)
						{
							float next_tetromino_x = offset_x + CELL_SIZE * mino.x;
							float next_tetromino_y = offset_y + CELL_SIZE * mino.y;
							cell.setPosition(sf::Vector2f(next_tetromino_x, next_tetromino_y));
							window.draw(cell);
						}

						draw_text(static_cast<unsigned short>(next_panel.getPosition().x + 6.f), static_cast<unsigned short>(next_panel.getPosition().y + 4.f), "Next", window);
					}
				};

				switch (state)
				{
					case GameState::Menu:
					{
						draw_playfield(false, true, false);
						window.draw(modal_shadow);
						window.draw(modal_back);
						unsigned short menu_y = static_cast<unsigned short>(modal_y + 12);
						draw_text(static_cast<unsigned short>(modal_x + 12), menu_y, "TETRIS\n\n1) Beginner\n2) Advanced\n3) High Scores\n4) Help\n5) Quit", window);
						break;
					}
					case GameState::HighScores:
					{
						draw_playfield(false, false, false);
						window.draw(modal_shadow);
						window.draw(modal_back);
						std::string scores_text = "High Scores\n";
						for (std::size_t i = 0; i < high_scores.size(); ++i)
						{
							scores_text += std::to_string(i + 1) + ". " + std::to_string(high_scores[i]) + "\n";
						}
						scores_text += "\nAny key to return";
						unsigned short hs_y = static_cast<unsigned short>(modal_y + 12);
						draw_text(static_cast<unsigned short>(modal_x + 12), hs_y, scores_text, window);
						break;
					}
					case GameState::Help:
					{
						draw_playfield(false, false, false);
						window.draw(modal_shadow);
						window.draw(modal_back);
						std::string help_text = "Help\nLeft/Right: Move\nZ/C: Rotate\nDown: Soft drop\nSpace: Hard drop\nP: Pause\nEnter: Menu (post game)\n\nAny key to return";
						unsigned short help_y = static_cast<unsigned short>(modal_y + 12);
						draw_text(static_cast<unsigned short>(modal_x + 12), help_y, help_text, window);
						break;
					}
					case GameState::Paused:
					case GameState::Playing:
					case GameState::GameOver:
					{
						unsigned short ui_x = static_cast<unsigned short>(stats_panel.getPosition().x + 4.f);
						unsigned short ui_y = static_cast<unsigned short>(stats_panel.getPosition().y + 6.f);
						draw_playfield(state != GameState::GameOver, false, true);

						if (has_scorebar)
						{
							scorebar_sprite.setPosition(sf::Vector2f(stats_panel.getPosition().x + 4.f, stats_panel.getPosition().y + 4.f));
							window.draw(scorebar_sprite);
						}

						std::string stats = "Score: " + std::to_string(score) +
							"\nLines: " + std::to_string(lines_cleared) +
							"\nLevel: " + std::to_string(level) +
							"\nSpeed: " + std::to_string(START_FALL_SPEED / current_fall_speed) + "x" +
							"\nLocked: " + std::to_string(locked_rows) +
							"\nTime: " + time_text +
							"\nMode: " + std::string(advanced_mode ? "Advanced" : "Beginner") +
							"\nBest: " + std::to_string(high_scores.front());
						draw_text(ui_x, ui_y, stats, window);

						if (state == GameState::Paused)
						{
							window.draw(modal_back);
							draw_text(static_cast<unsigned short>(modal_x + 8), static_cast<unsigned short>(modal_y + 8), "Paused\n1) Beginner\n2) Advanced\n3) High Scores\n4) Help\n5) Continue\nEnter for menu", window);
						}
						else if (state == GameState::GameOver)
						{
							window.draw(modal_back);
							draw_text(static_cast<unsigned short>(modal_x + 8), static_cast<unsigned short>(modal_y + 8), "Game Over\nScore:" + std::to_string(score) + "\nEnter for menu", window);
						}
						break;
					}
				}

				window.display();
			}
		}
	}
}