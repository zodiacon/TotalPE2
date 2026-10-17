#pragma once

#include <Win32Api.h>

//
// The icons of the lists, the tree and the tabs: one image list for all of them
//
namespace Images {
	enum Index {
		Function, Callback, Struct, Union, Enum, Typedef, Interface, Constant, Namespace, Folder, FolderOpen,
		Count
	};

	// of the host's icons (of its resources), in the order of Index
	inline CImageList Create(UINT const (&icons)[Count]) {
		CImageList images;
		images.Create(16, 16, ILC_COLOR32 | ILC_MASK, Count, 4);
		for (auto id : icons)
			images.AddIcon(AtlLoadIconImage(id, LR_DEFAULTCOLOR, 16, 16));
		return images;
	}

	inline int Of(Win32Api::Entry::Kinds kind) {
		using Kinds = Win32Api::Entry::Kinds;
		switch (kind) {
			case Kinds::Function: return Function;
			case Kinds::Callback: return Callback;
			case Kinds::Struct: return Struct;
			case Kinds::Union: return Union;
			case Kinds::Enum: return Enum;
			case Kinds::Typedef: return Typedef;
			case Kinds::Interface: return Interface;
			case Kinds::Constant: return Constant;
		}
		return Function;
	}
}
