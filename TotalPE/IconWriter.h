#pragma once

struct IconWriter final abstract {
public:
	static bool Save(PCWSTR path, HICON const hIcon, int colors, bool icon = true);
};

