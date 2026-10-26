#pragma once

#include <atluser.h>
#include <Win32Api.h>
#include <DpiHelper.h>

//
// The icons of the lists, the tree and the tabs: one image list for all of them, in the small icon size of a DPI
//
namespace Images {
	enum Index {
		Function, Callback, Struct, Union, Enum, Typedef, Interface, Constant, Namespace, Folder, FolderOpen,
		Count
	};

	inline void Add(HIMAGELIST images, UINT const (&icons)[Count]) {
		int size, cy;
		::ImageList_GetIconSize(images, &size, &cy);
		for (auto id : icons) {
			CIcon icon(AtlLoadIconImage(id, LR_DEFAULTCOLOR, size, size));
			::ImageList_AddIcon(images, icon);
		}
	}

	// of the host's icons (of its resources), in the order of Index; dpi 0: the system's
	inline CImageList Create(UINT const (&icons)[Count], UINT dpi = 0) {
		auto size = DpiHelper::SmallIconSize(dpi ? dpi : ::GetDpiForSystem());
		CImageList images;
		images.Create(size, size, ILC_COLOR32 | ILC_MASK, Count, 4);
		Add(images, icons);
		return images;
	}

	// the icons in the size of another DPI, in the same image list (the controls have it): true if it changed, and the
	// controls that have it are to take it again (which measures their items again)
	inline bool SetDpi(HIMAGELIST images, UINT const (&icons)[Count], UINT dpi) {
		auto size = DpiHelper::SmallIconSize(dpi);
		int cx, cy;
		if (!images || !::ImageList_GetIconSize(images, &cx, &cy) || cx == size)
			return false;
		::ImageList_SetIconSize(images, size, size);	// (the images are removed)
		Add(images, icons);
		return true;
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
