#pragma once

#include "Interfaces.h"

// Saving what the views show to files: lists, resources and sections

// The list or text control of a view: the one with the focus, or else the first that is visible (null if there is none)
HWND FindViewControl(HWND hView, PCWSTR className);

// A name that can be used for a file: no characters that file names cannot have
CString ToFileName(CString name);

// The path the user picks, empty if the dialog is canceled
CString AskSaveFile(HWND hOwner, PCWSTR title, PCWSTR defExt, PCWSTR fileName, PCWSTR filter);
CString AskFolder(HWND hOwner, PCWSTR title);

bool WriteFileData(PCWSTR path, void const* data, size_t size);

// The rows of a list view, in the order and with the columns that are shown: as CSV, or as tab separated text if the user
// picks a name that does not end with .csv. Asks for the file name; reports failures.
bool SaveListView(HWND hOwner, HWND hList, CString const& name);

// Resources as files (a bitmap gets its file header, an icon is an .ico file...): one resource asks for a file name,
// more for a folder that gets a file for each. Reports failures. 'all' are the resources of the file: an icon or cursor group
// is saved with its images (which are resources of their own) as one .ico or .cur file.
bool SaveResourceFiles(HWND hOwner, std::vector<FlatResource const*> const& resources, std::vector<FlatResource> const& all);

// The data of a section in the file (its raw data)
bool SaveSectionData(HWND hOwner, PEFile const& pe, PESectionHeader const& section);
