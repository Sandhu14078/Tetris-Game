#include <SFML/Graphics.hpp>
#include <array>
#include <iostream>

#include "Headers/DrawText.hpp"

void draw_text(unsigned short i_x, unsigned short i_y, const std::string& i_text, sf::RenderWindow& i_window)
{
	static sf::Font font;
	static bool font_loaded = false;
	static bool warned = false;
	if (!font_loaded)
	{
		const std::array<std::string, 4> font_paths = {
			"Resources/Images/Font.ttf",
			"/mingw64/share/fonts/TTF/DejaVuSans.ttf",
			"C:/Windows/Fonts/arial.ttf",
			"C:/Windows/Fonts/segoeui.ttf"
		};
		for (const auto& path : font_paths)
		{
			if (font.openFromFile(path))
			{
				font_loaded = true;
				break;
			}
		}
		if (!font_loaded && !warned)
		{
			std::cerr << "Failed to load any font. Checked: Resources/Images/Font.ttf, DejaVuSans, Arial, Segoe UI." << std::endl;
			warned = true;
		}
	}

	if (!font_loaded) return;

	sf::Text text(font);
	text.setCharacterSize(10);
	text.setFillColor(sf::Color::White);

	float x = static_cast<float>(i_x);
	float y = static_cast<float>(i_y);
	std::string line;
	for (char ch : i_text)
	{
		if (ch == '\n')
		{
			text.setString(line);
			text.setPosition(sf::Vector2f(x, y));
			i_window.draw(text);
			line.clear();
			y += text.getCharacterSize();
			continue;
		}
		line.push_back(ch);
	}
	text.setString(line);
	text.setPosition(sf::Vector2f(x, y));
	i_window.draw(text);
}