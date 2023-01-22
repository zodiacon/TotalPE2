#include "pch.h"
#include "Selection.h"

void Selection::SetSimple(int64_t offset, int64_t len) {
	_type = SelectionType::Simple;
	_offset = offset;
	_length = len;
}

void Selection::SetBox(int64_t offset, int bytesPerLine, int width, int height) {
	_type = SelectionType::Box;
	_offset = offset;
	_width = width;
	_height = height;
	_bytesPerLine = bytesPerLine;
}

void Selection::SetAnchor(int64_t offset) {
	_anchor = offset;
}

int64_t Selection::GetOffset() const {
	return _offset;
}

int64_t Selection::GetAnchor() const {
	return _anchor;
}

bool Selection::IsSelected(int64_t offset) const {
	switch (_type) {
		case SelectionType::Simple:
			return offset >= _offset && offset < _offset + _length;

		case SelectionType::Box:
			if (offset < _offset)
				return false;
			return (offset - _offset) % _bytesPerLine < _width && (offset - _offset) / _bytesPerLine < _height;
	}
	ATLASSERT(false);
	return false;
}

bool Selection::IsEmpty() const {
	return _offset < 0;
}

SelectionType Selection::GetSelectionType() const {
	return _type;
}

int64_t Selection::GetLength() const {
	return _length;
}

void Selection::Clear() {
	_type = SelectionType::Simple;
	_offset = _anchor = -1;
	_length = 0;
}
