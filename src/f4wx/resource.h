// {{NO_DEPENDENCIES}}
// F4Wx resource IDs ? used by resource.rc and application code.
//
// ID namespace plan (ranges are reserved per category):
//   100?129   Icons (IDI_), Dialogs (IDD_)
//   130?199   Reserved (APSTUDIO next resource)
//   200?299   Bitmaps (IDB_): custom map, theater maps
//  1000?1099  Main dialog controls (IDC_F4WX_MAIN_*)
//  1100?1199  Progress dialog controls (IDC_F4WX_PROGRESS_*)
//  1200?1299  GFS selector dialog (IDC_F4WX_GFS_*)
//  1300?1399  About dialog (IDC_F4WX_ABOUT_*)
//  1400?1499  Custom dialog (IDC_F4WX_CUSTOM_*)
//  1500?1599  Wind levels dialog (IDC_F4WX_WIND*)
//  4000?4999  Menus (IDM_*)
//

/////////////////////////////////////////////////////////////////////////////
// Icons (100?129)
/////////////////////////////////////////////////////////////////////////////
#define IDI_ICON1                               103
#define IDI_ICON_PAUSE                          122
#define IDI_ICON_PLAY                           123

/////////////////////////////////////////////////////////////////////////////
// Dialogs (100?129)
/////////////////////////////////////////////////////////////////////////////
#define IDD_F4WX_MAIN                           110
#define IDD_F4WX_PROGRESS                       111
#define IDD_F4WX_GFS_SELECTOR                   113
#define IDD_F4WX_ABOUT                          115
#define IDD_F4WX_CUSTOM                         117
#define IDD_F4WX_WINDLEVELS                     128

/////////////////////////////////////////////////////////////////////////////
// Bitmaps (200?299)
/////////////////////////////////////////////////////////////////////////////
#define IDB_F4WX_CUSTOM_MAP                     121
#define IDB_THEATER_KOREA                       201
#define IDB_THEATER_AEGEAN                      202
#define IDB_THEATER_BALKANS                     203
#define IDB_THEATER_EMF                         204
#define IDB_THEATER_HTO                         205
#define IDB_THEATER_ISRAEL                      206
#define IDB_THEATER_KURILE                      207
#define IDB_THEATER_KUWAIT                      208
#define IDB_THEATER_MIDEAST                      209
#define IDB_THEATER_NEVADA                       210
#define IDB_THEATER_NORDIC                       211
#define IDB_THEATER_OSTSEE                       212
#define IDB_THEATER_PANAMA                       213
#define IDB_THEATER_POH                          214
#define IDB_THEATER_TAIWAN                       215
#define IDB_THEATER_VIETNAM                      216

/////////////////////////////////////////////////////////////////////////////
// Main dialog controls (1000?1099)
/////////////////////////////////////////////////////////////////////////////
#define IDC_F4WX_MAIN_THEATER_SELECTOR          1000
#define IDC_F4WX_MAIN_DOWNLOAD                  1001
#define IDC_F4WX_MAIN_CLOUD_COVER_FAIR          1002
#define IDC_F4WX_MAIN_LOAD_FILE                  1003
#define IDC_F4WX_MAIN_SAVE_SINGLE                1004
#define IDC_F4WX_MAIN_START_CURRENT              1005
#define IDC_F4WX_MAIN_CLOUD_COVER_POOR          1006
#define IDC_F4WX_MAIN_CLOUD_COVER_INCLEMENT     1007
#define IDC_F4WX_MAIN_SAVE                      1008
#define IDC_F4WX_MAIN_SLIDER                    1009
#define IDC_F4WX_MAIN_SAVE_PREVIEWS             1010
#define IDC_F4WX_MAIN_PRECIPIT_FAIR             1011
#define IDC_F4WX_MAIN_PRECIPIT_POOR             1012
#define IDC_F4WX_MAIN_PRECIPIT_INCLEMENT         1013
#define IDC_F4WX_MAIN_BMS_TIME_PREFFIX          1014
#define IDC_F4WX_MAIN_BMS_TIME                  1015
#define IDC_F4WX_MAIN_TIME_INTERVAL             1016
#define IDC_F4WX_MAIN_CURRENT_TIME_PREFIX        1017
#define IDC_F4WX_MAIN_FORECAST_TIME             1018
#define IDC_F4WX_MAIN_CURRENT_TIME              1019
#define IDC_F4WX_MAIN_CAMPAIGN_DAY              1020
#define IDC_F4WX_MAIN_CAMPAIGN_HOUR             1021
#define IDC_F4WX_MAIN_CAMPAIGN_MINUTE           1022
#define IDC_F4WX_MAIN_SAVE_SEQUENCE             1023
#define IDC_F4WX_MAIN_SYNC_TIMEZONE             1024
#define IDC_F4WX_MAIN_WARN_SYNC                 1025
#define IDC_F4WX_MAIN_WARN_INTERVAL             1026
#define IDC_F4WX_MAIN_WARN_FORECAST             1027
#define IDC_F4WX_MAIN_PREVIEW                   1028
#define IDC_F4WX_MAIN_PLAY                      1029
#define IDC_F4WX_MAIN_PREVIEW_CLOUDS            1030
#define IDC_F4WX_MAIN_PREVIEW_TEMPERATURE       1031
#define IDC_F4WX_MAIN_PREVIEW_PRESSURE          1032
#define IDC_F4WX_MAIN_PREVIEW_WINDS             1033
#define IDC_F4WX_MAIN_ABOUT                     1034
#define IDC_F4WX_MAIN_NODATA                    1035
#define IDC_F4WX_MAIN_MINUTE                    1036
#define IDC_F4WX_MAIN_WARN_TIMEZONE             1037
#define IDC_F4WX_MAIN_WARN_START                1038

// Legacy aliases (same control as above)
#define IDC_F4WX_THEATER_SELECTOR               IDC_F4WX_MAIN_THEATER_SELECTOR
#define IDC_PLAY                                IDC_F4WX_MAIN_PLAY
#define IDC_MAIN_PREVIEW                        IDC_F4WX_MAIN_PREVIEW

/////////////////////////////////////////////////////////////////////////////
// Progress dialog controls (1100-1199)
/////////////////////////////////////////////////////////////////////////////
#define IDC_F4WX_PROGRESS_BAR                   1100
#define IDC_F4WX_PROGRESS_TEXT                  1101

/////////////////////////////////////////////////////////////////////////////
// GFS selector dialog (1200-1299)
/////////////////////////////////////////////////////////////////////////////
#define IDC_F4WX_GFS_LIST                       1200
#define IDC_F4WX_GFS_FORECAST                   1201
#define IDC_F4WX_GFS_INTERVAL                   1202

/////////////////////////////////////////////////////////////////////////////
// About dialog (1300-1399)
/////////////////////////////////////////////////////////////////////////////
#define IDC_F4WX_ABOUT_TEXT                     1300

/////////////////////////////////////////////////////////////////////////////
// Custom dialog (1400-1499)
/////////////////////////////////////////////////////////////////////////////
#define IDC_F4WX_CUSTOM_LLON                    1400
#define IDC_F4WX_CUSTOM_RLON                    1401
#define IDC_F4WX_CUSTOM_TLAT                    1402
#define IDC_F4WX_CUSTOM_BLAT                    1403
#define IDC_F4WX_CUSTOM_TIMEZONE                1404
#define IDC_F4WX_CUSTOM_THEATER_SIZE            1405
#define IDC_F4WX_DEBUG_TEXT                     1406

/////////////////////////////////////////////////////////////////////////////
// Wind levels dialog (1500-1599); IDs must be consecutive for IDC_F4WX_WIND0 + n
/////////////////////////////////////////////////////////////////////////////
#define IDC_F4WX_WIND0                          1500
#define IDC_F4WX_WIND1                          1501
#define IDC_F4WX_WIND2                          1502
#define IDC_F4WX_WIND3                          1503
#define IDC_F4WX_WIND4                          1504
#define IDC_F4WX_WIND5                          1505
#define IDC_F4WX_WIND6                          1506
#define IDC_F4WX_WIND7                          1507
#define IDC_F4WX_WIND8                          1508
#define IDC_F4WX_WIND9                          1509

/////////////////////////////////////////////////////////////////////////////
// Menus (4000-4999)
/////////////////////////////////////////////////////////////////////////////
#define IDM_F4WX_PREVIEW_MENU                   4000
#define IDM_F4WX_PREVIEW_MENU_SAVE              4001
#define IDM_F4WX_PREVIEW_MENU_LOAD              4002

/////////////////////////////////////////////////////////////////////////////
// APSTUDIO next-value hints (do not assign IDs in these ranges manually)
/////////////////////////////////////////////////////////////////////////////
#ifdef APSTUDIO_INVOKED
#ifdef APSTUDIO_READONLY_SYMBOLS
#define _APS_NEXT_RESOURCE_VALUE                130
#define _APS_NEXT_COMMAND_VALUE                 4003
#define _APS_NEXT_CONTROL_VALUE                 1510
#define _APS_NEXT_SYMED_VALUE                   101
#endif
#endif
