//
// The commands of the Windows API browser (CApiBrowser), in a range of their own (ID_APIBROWSER_FIRST to _LAST): its host's
// menus and toolbars can have them, and pass them on to it. The groups shown as radio items are of consecutive IDs.
// (Only #defines: a resource script includes it, as read-only symbols.)
//
#pragma once

#define ID_APIBROWSER_FIRST         40960

#define ID_GO_BACK                  40960
#define ID_GO_FORWARD               40961
#define ID_GO_DEFINITION            40962
#define ID_GO_DEFINITION_NEWTAB     40963
#define ID_GO_REVEAL                40964
#define ID_GO_USEDBY                40965
#define ID_GO_QUICKOPEN             40966
#define ID_GO_SIDE                  40967
#define ID_DOC_BROWSER              40968

// the pages of the navigator, in their order (the host's list: if it gives one, as TotalPE's Imports)
#define ID_VIEW_SEARCH              40970
#define ID_VIEW_NAMESPACES          40971
#define ID_VIEW_USEDBY              40972
#define ID_VIEW_FAVORITES           40973
#define ID_VIEW_HISTORY             40974
#define ID_VIEW_HOSTLIST            40975
#define ID_VIEW_CLEARHISTORY        40976
#define ID_VIEW_DOCUMENTATION       40977

#define ID_EDIT_COPYNAME            40980
#define ID_EDIT_DECODE              40981
#define ID_EDIT_FAVORITE            40982
#define ID_EDIT_COPYCSHARP          40983
#define ID_EDIT_COPYRUST            40984
#define ID_EDIT_COPYPYTHON          40985

#define ID_SHOW_DECLARATION         40990
#define ID_SHOW_LAYOUT              40991
#define ID_SHOW_COMPARE             40992

#define ID_TAB_CLOSE                41000
#define ID_TAB_CLOSEOTHERS          41001
#define ID_TAB_CLOSEALL             41002
#define ID_TAB_NEXT                 41003
#define ID_TAB_PREV                 41004
#define ID_TAB_KEEPOPEN             41005

#define ID_APIBROWSER_LAST          41023
