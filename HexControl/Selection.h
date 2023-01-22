#pragma once

enum class SelectionType {
	Simple,
	Box
};

class Selection {
public:
	void SetSimple(int64_t offset, int64_t len);
	void SetBox(int64_t offset, int bytesPerLine, int width, int height);
	void SetAnchor(int64_t offset);

	int64_t GetOffset() const;
	int64_t GetAnchor() const;
	bool IsSelected(int64_t offset) const;
	bool IsEmpty() const;
	SelectionType GetSelectionType() const;
	int64_t GetLength() const;

	void Clear();

private:
	SelectionType _type{ SelectionType::Simple };
	int64_t _offset{ -1 }, _anchor{ -1 };
	int64_t _length{ 0 };
	int _width, _height, _bytesPerLine;
};

