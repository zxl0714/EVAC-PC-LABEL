#pragma once

// English captions and verified sprite rectangles for Korean Battle Garegga 01.00.
namespace cave::ps4::garegga {

inline constexpr GareggaCaption kPause[]{
    {673, 320, 86, 28, false, L"... OK"}, // 0008
    {761, 297, 226, 28, false, L"... Pause Menu"}, // 0009
    {873, 1, 138, 28, false, L"... Resume"}, // 0010
    {463, 227, 276, 31, false, L"... Game Settings"}, // 0011
    {873, 31, 136, 27, false, L"Hold"}, // 0012
    {805, 436, 62, 28, false, L"Ship 1"}, // 0026
    {955, 231, 66, 28, false, L"Ship 2"}, // 0027
    {689, 260, 66, 28, false, L"Ship 3"}, // 0028
    {935, 383, 66, 28, false, L"Ship 4"}, // 0029
    {463, 459, 48, 27, false, L"Gain"}, // 0030
    {967, 191, 50, 28, false, L"Chitta"}, // 0031
    {593, 436, 100, 27, false, L"Miyamoto"}, // 0032
    {699, 291, 50, 27, false, L"Bornnam"}, // 0033
    {463, 436, 128, 21, false, L"Before Select"}, // 0034
    {629, 377, 130, 21, false, L"After Select"}, // 0035
    {873, 448, 110, 21, false, L"After Load"}, // 0036
};

inline constexpr GareggaCaption kCommon[]{
    {3601, 378, 484, 29, true, L"Return to the paused game."}, // 0129
    {3531, 1173, 564, 29, true, L"Restart at the selected point."}, // 0130
    {2455, 1232, 370, 29, true, L"Open Option Settings."}, // 0131
    {1975, 920, 548, 61, true, L"Save your current progress.\nSlots are separate for each game mode."}, // 0132
    {1003, 679, 816, 61, true, L"Load a quick save and resume playing.\nReplay saving and ranking submissions are disabled."}, // 0133
    {1975, 1170, 636, 29, true, L"Quit the current run and reset the game."}, // 0134
    {1687, 460, 280, 29, true, L"View the rankings."}, // 0135
    {2879, 921, 916, 31, true, L"Open the Archive Room to view replays, artwork and more."}, // 0136
    {1975, 1047, 806, 29, true, L"Change the game mode or BGM set. Changes require a reset."}, // 0137
    {3675, 985, 402, 28, true, L"Select a game mode."}, // 0138
    {1003, 258, 896, 93, true, L"Select the base mode for Custom.\nMode Details inherits that mode's defaults.\nSelect NO CHANGE to keep your current Custom settings."}, // 0139
    {1, 1222, 524, 29, true, L"Select a BGM set."}, // 0140
    {2879, 858, 648, 61, true, L"Select one of four difficulty levels,\nas on the arcade board's DIP switches."}, // 0141
    {689, 1131, 658, 29, true, L"Keep gameplay rank fixed."}, // 0142
    {1, 1069, 788, 29, true, L"Adjust enemy durability. 100% is the arcade value."}, // 0143
    {791, 1069, 788, 29, true, L"Adjust enemy bullet density. 100% is the arcade value."}, // 0144
    {837, 1038, 814, 29, true, L"Adjust enemy bullet speed. 100% is the arcade value."}, // 0145
    {1975, 591, 894, 61, true, L"Use fixed boss attack patterns instead of random ones.\nPatterns differ between Super Easy and Premium."}, // 0146
    {2879, 1143, 674, 28, true, L"Enable the original arcade Stage Edit feature."}, // 0147
    {1975, 794, 756, 61, true, L"Enable the arcade's EXTENDED, HARDER and SPECIAL modes."}, // 0148
    {689, 1223, 522, 29, true, L"Set the starting number of lives."}, // 0149
    {3683, 1234, 360, 29, true, L"Set the extend score."}, // 0150
    {1, 723, 810, 61, true, L"Set the items dropped by the player plane in CLOUD\nwhen it breaks apart."}, // 0151
    {1, 1191, 618, 29, true, L"Set the number of special weapons at start and after a death."}, // 0152
    {689, 1162, 640, 29, true, L"Set the weapon gauge gain from small weapon items."}, // 0153
    {1975, 259, 892, 93, true, L"Fire a special weapon instead of losing a life if you have enough.\nDuring focused fire, this only works while holding Shot."}, // 0154
    {1975, 461, 896, 63, true, L"Set how fast items fall as rank changes.\nOther settings use a fixed speed."}, // 0155
    {2879, 1203, 556, 29, true, L"Make medals easier to collect by drawing them toward you."}, // 0156
    {1309, 1193, 582, 29, true, L"Set the penalty for missing a medal."}, // 0157
    {2879, 954, 896, 29, true, L"Extend invulnerability while using a special weapon."}, // 0158
    {1309, 1224, 466, 29, true, L"Adjust the size of your ship's hitbox."}, // 0159
    {1003, 877, 636, 61, true, L"Reduce in-game input delay for slower displays."}, // 0160
    {1975, 526, 886, 63, true, L"Auto-submit new personal bests.\nWith this off, the game asks before submitting."}, // 0161
    {1003, 161, 898, 95, true, L"Auto-save a replay after each run.\nWith this off, the game asks before saving.\nIf slots are full, overwrite the lowest score."}, // 0162
    {3499, 423, 576, 61, true, L"Unlock Gain, Chitta, Miyamoto and Bornnam."}, // 0163
    {1, 914, 568, 61, true, L"Color selected enemy bullets.\nChange the setting to make them flash."}, // 0164
    {2879, 1081, 762, 29, true, L"Set the maximum rapid-fire rate the game will accept."}, // 0165
    {1975, 1078, 768, 29, true, L"Choose the restart point used by the Quick Restart button."}, // 0166
    {1003, 812, 660, 63, true, L"Turn on Free Play.\nWhen off, insert a credit before starting."}, // 0167
    {1, 358, 776, 93, true, L"Adjust how much rank rises over time.\nThis multiplies the overall rank setting.\nAvailable while Fixed Rank is off."}, // 0168
    {1, 849, 652, 63, true, L"Set the in-game rapid-fire rate.\nArcade uses the player's actual firing rate."}, // 0169
    {1003, 973, 896, 29, true, L"Set the points for taking weapon items at full stock\nand for destroying enemies with a special weapon."}, // 0170
    {2879, 1234, 430, 29, true, L"Lock the player's power-up level."}, // 0171
    {1975, 857, 654, 61, true, L"Adjust the overall rank change.\nAvailable while Fixed Rank is off."}, // 0172
    {1, 583, 692, 80, true, L"Adjust rank changes from items and hits.\nThis multiplies the overall rank setting.\nAvailable while Fixed Rank is off."}, // 0173
    {1975, 1201, 574, 29, true, L"Turn sound on or off in attract mode."}, // 0174
    {1, 1253, 334, 29, true, L"Allow continues."}, // 0175
    {3601, 316, 372, 60, true, L"Show or hide all Gadgets."}, // 0176
    {1975, 1109, 718, 29, true, L"Choose a Gadget layout preset."}, // 0177
    {3531, 1204, 554, 28, true, L"Show the total score for the current run."}, // 0178
    {3529, 858, 554, 61, true, L"Show the total score for the current run.\nLarge version for vertical screens."}, // 0179
    {1975, 1017, 890, 28, true, L"Show the score and high score for each stage."}, // 0180
    {1, 1100, 744, 29, true, L"Show the current controller layout."}, // 0181
    {1003, 940, 894, 31, true, L"Graph the current difficulty rank in real time."}, // 0182
    {3499, 486, 592, 29, true, L"Show the selected ship and remaining lives."}, // 0183
    {689, 1193, 618, 28, true, L"Show your remaining special weapons."}, // 0184
    {1, 1038, 834, 29, true, L"Show the time remaining before the boss escapes."}, // 0185
    {2879, 1050, 796, 29, true, L"Show remaining boss or mid-boss health."}, // 0186
    {1, 977, 896, 29, true, L"Show the upcoming drop from enemies that use the item table."}, // 0187
    {1, 1161, 670, 28, true, L"Show the rapid-fire rate the game currently detects."}, // 0188
    {2879, 795, 722, 61, true, L"Show hidden Option formation requirements\nand current progress in real time."}, // 0189
    {2879, 1112, 694, 29, true, L"Show the current track, version, composer and arranger."}, // 0190
    {1975, 1232, 478, 28, true, L"Show the left half of the instruction card."}, // 0191
    {3555, 1143, 502, 28, true, L"Show the right half of the instruction card."}, // 0192
    {1687, 491, 278, 28, true, L"Show the M2 logo."}, // 0193
    {3779, 221, 314, 29, true, L"Show the Eighting logo."}, // 0194
    {2879, 592, 894, 61, true, L"Graph the current difficulty rank.\nCompact version for vertical screens."}, // 0195
    {1, 1008, 896, 28, true, L"Show the My Set and detected rapid-fire rates."}, // 0196
    {1003, 460, 682, 87, true, L"Show hidden Option formation requirements and progress.\nCompact version for vertical screens."}, // 0197
    {2879, 1173, 650, 28, true, L"Show the left half of the Rev.2016 instruction card."}, // 0198
    {1975, 1140, 676, 28, true, L"Show the right half of the Rev.2016 instruction card."}, // 0199
    {2879, 221, 898, 93, true, L"Rotate the picture. Use a monitor that can rotate\nfor vertical play. Avoid turning a fixed display."}, // 0200
    {1, 786, 782, 61, true, L"Choose a screen preset.\nEditing its settings switches the preset to CUSTOM."}, // 0201
    {2879, 423, 618, 107, true, L"Choose a screen size mode.\nFill: stretch to the screen's edges.\nInteger: scale the original size by a whole number.\nManual: adjust width and height."}, // 0202
    {1, 263, 842, 93, true, L"Adjust width and height based on the screen size mode.\nPress Circle to open the editor.\nAvailable with Manual screen sizing."}, // 0203
    {2879, 1019, 838, 29, true, L"Smooth the edges between pixels."}, // 0204
    {1, 1131, 686, 28, true, L"Add CRT-style scanlines."}, // 0205
    {747, 1100, 744, 29, true, L"Choose whether to show wallpaper behind the game."}, // 0206
    {3311, 1234, 370, 29, true, L"Press the button to open settings."}, // 0207
};

inline constexpr GareggaCaption kDialog[]{
    {825, 600, 136, 27, false, L"Hold"}, // 0015
    {1973, 114, 42, 50, true, L"Yes"}, // 0016
    {1715, 1132, 140, 52, true, L"No"}, // 0017
    {547, 1687, 398, 32, false, L"Do not show this again."}, // 0018
    {1925, 61, 100, 28, false, L"Back to Game"}, // 0019
    {695, 1545, 190, 28, false, L"Pause Menu"}, // 0020
    {631, 854, 284, 29, false, L"Start 2-Player Game"}, // 0021
    {825, 629, 130, 28, false, L"Continue"}, // 0022
    {755, 1257, 192, 29, false, L"Reset Game"}, // 0023
    {695, 1485, 242, 28, false, L"Overwrite Save"}, // 0024
    {737, 1388, 218, 28, false, L"Load Save"}, // 0025
    {695, 1423, 254, 29, false, L"Apply and Reset"}, // 0026
    {631, 793, 318, 29, false, L"Go Back"}, // 0027
    {631, 824, 320, 28, false, L"Restore Previous Settings"}, // 0028
    {737, 1357, 216, 29, false, L"Controller Settings"}, // 0029
    {755, 1288, 192, 29, false, L"Resume Game"}, // 0030
    {695, 1515, 216, 28, false, L"Replay List"}, // 0031
    {1657, 1487, 242, 28, false, L"Delete Replay"}, // 0032
    {695, 1454, 242, 29, false, L"Save Replay"}, // 0033
    {1703, 1329, 334, 29, false, L"Resume Replay"}, // 0034
    {755, 1319, 192, 28, false, L"Start Game"}, // 0035
    {1925, 31, 102, 28, false, L"Create New"}, // 0036
    {695, 1575, 164, 29, false, L"Ranking List"}, // 0037
    {1703, 1360, 296, 26, false, L"Replay Theater"}, // 0038
    {1, 1227, 752, 63, true, L"Creating save data...\nKeep the game open while saving."}, // 0039
    {1, 1095, 752, 64, true, L"Saving your settings...\nKeep the game open while saving."}, // 0040
    {963, 1264, 752, 63, true, L"Saving replay data...\nKeep the game open while saving."}, // 0041
    {1, 963, 908, 64, true, L"Scores cannot be ranked in two-player games.\nStart a two-player game?"}, // 0042
    {1, 1161, 744, 64, true, L"Scores cannot be ranked after a continue.\nContinue?"}, // 0043
    {963, 1395, 692, 64, true, L"Reset the game? You will lose the current run."}, // 0044
    {1, 1357, 734, 64, true, L"Save the current run to this slot?\nThe existing save will be overwritten."}, // 0045
    {1, 793, 628, 99, true, L"Load this save? The current system settings\nmust change to match the save."}, // 0046
    {1, 1423, 692, 64, true, L"Applying these settings requires a reset.\nReset the game and discard the current run?"}, // 0047
    {1, 703, 836, 88, true, L"Restore the previous button layout?\nMap Shot, Weapon, Option and Credit.\nAlso map Rapid Speed if you use My Set."}, // 0048
    {963, 1198, 744, 64, true, L"Changing the BGM set requires a reset.\nReset the game and discard the current run?"}, // 0049
    {1657, 1395, 378, 29, true, L"Could not load the rankings."}, // 0050
    {547, 1721, 394, 28, true, L"Uploading score..."}, // 0051
    {1371, 1884, 508, 28, true, L"Uploading replay..."}, // 0052
    {1, 1489, 602, 64, true, L"Could not upload the score or replay.\nThe score will be sent the next time you connect."}, // 0053
    {1, 1913, 472, 29, true, L"Downloading replay..."}, // 0054
    {963, 1329, 738, 64, true, L"The replay recording is full.\nContinue with the replay recorded so far?"}, // 0055
    {1371, 1822, 616, 29, true, L"Exit without saving the replay?"}, // 0056
    {963, 1527, 596, 63, true, L"At least one replay must stay unlocked.\nUnlock a replay you no longer need."}, // 0057
    {1, 1686, 542, 63, true, L"This replay is locked.\nUnlock it before overwriting."}, // 0058
    {1561, 1527, 430, 63, true, L"This replay is locked.\nUnlock it before deleting."}, // 0059
    {1371, 1853, 594, 29, true, L"Delete this replay?"}, // 0060
    {1, 1029, 898, 64, true, L"Overwrite this replay?"}, // 0061
    {1509, 1592, 406, 64, true, L"You can now insert up to 3 credits."}, // 0062
    {547, 1621, 406, 64, true, L"You can now insert up to 4 credits."}, // 0063
    {963, 1756, 406, 64, true, L"You can now insert up to 5 credits."}, // 0064
    {1371, 1756, 406, 64, true, L"You can now insert up to 6 credits."}, // 0065
    {1, 1785, 406, 64, true, L"You can now insert up to 7 credits."}, // 0066
    {409, 1785, 406, 64, true, L"You can now insert up to 8 credits."}, // 0067
    {963, 1822, 406, 64, true, L"You can now insert up to 9 credits."}, // 0068
    {963, 997, 908, 64, true, L"Unlimited credits unlocked.\nEnable Free Play in Other Settings."}, // 0069
    {963, 1888, 404, 29, true, L"No further information is available."}, // 0070
    {1657, 1426, 390, 28, true, L"Replay deleted."}, // 0071
    {1509, 1658, 528, 29, true, L"Stop playing the replay?"}, // 0072
    {1, 1751, 860, 32, true, L"You may be signed out of PlayStation Network."}, // 0073
    {1, 1621, 544, 63, true, L"The system save is corrupt.\nCreating a replacement save."}, // 0074
    {963, 1658, 544, 63, true, L"The quick save is corrupt.\nCreating a replacement save."}, // 0075
    {963, 1592, 544, 64, true, L"The replay save is corrupt.\nCreating a replacement save."}, // 0076
    {1, 894, 908, 67, true, L"Replay sharing is restricted on this account.\nIf you continue, your score will not be ranked."}, // 0077
    {963, 1063, 812, 67, true, L"Replay sharing is restricted on this account."}, // 0078
    {1657, 1456, 366, 29, true, L"A network error occurred."}, // 0079
    {1, 1292, 752, 63, true, L"Saving quick-save data...\nKeep the game open while saving."}, // 0080
    {963, 1723, 908, 31, true, L"Scores cannot be ranked in two-player games."}, // 0081
    {1237, 956, 744, 31, true, L"Scores cannot be ranked after a continue."}, // 0082
    {1, 1882, 728, 29, true, L"Only solo games played on the 1P side can be ranked."}, // 0083
    {475, 1913, 420, 28, true, L"Loading replay data..."}, // 0084
    {1, 600, 822, 101, true, L"At least 111 MB is required for save data.\nIf you continue, settings, scores and replays will not be saved."}, // 0085
    {1237, 890, 790, 64, true, L"Open Replay Theater? The current run will be ineligible\nfor replay saving and ranking submissions."}, // 0086
    {1237, 723, 712, 99, true, L"All of your replays are locked.\nUnlock a replay in the list before overwriting it."}, // 0087
    {1, 1555, 582, 64, true, L"Old quick-save data was found.\nCreating a replacement save."}, // 0088
    {963, 1132, 750, 64, true, L"Play the replay? The current run will be ineligible\nfor replay saving and ranking submissions."}, // 0089
    {1, 1851, 754, 29, true, L"Assign at least one controller to either 1P or 2P."}, // 0090
    {1, 461, 888, 137, true, L"Controller assignments reset on restart if you use\nmultiple controllers or assign a controller to 2P.\nSet the assignments again after restarting."}, // 0091
    {963, 1461, 692, 64, true, L"Open the user selection screen?\nYou will lose the current run."}, // 0092
    {1509, 1689, 526, 28, true, L"Could not download the replay."}, // 0093
    {1237, 824, 804, 64, true, L"Reset Option Settings?\nLocal scores, quick saves and replays will be kept."}, // 0094
    {1925, 91, 118, 21, true, L"(Hold)"}, // 0095
    {1971, 214, 60, 27, false, L"Yes"}, // 0096
    {1925, 1, 112, 28, false, L"No"}, // 0097
    {1715, 1216, 140, 28, false, L"Check"}, // 0098
    {1715, 1186, 148, 28, false, L"Uncheck"}, // 0099
};

inline constexpr GareggaCaption kOptions[]{
    {1473, 1113, 76, 27, false, L"Width"}, // 0023
    {1165, 1149, 76, 27, false, L"Height"}, // 0024
    {4006, 871, 86, 28, false, L"... OK"}, // 0027
    {1833, 1016, 86, 28, false, L"... Cancel"}, // 0028
    {3838, 1117, 226, 28, false, L"... Pause Menu"}, // 0029
    {963, 964, 276, 31, false, L"... Game Settings"}, // 0030
    {2730, 787, 148, 28, false, L"... Rapid Fire"}, // 0031
    {1243, 1022, 264, 29, false, L"... Assign Controllers"}, // 0032
    {3170, 1011, 74, 27, false, L"Hold"}, // 0033
    {3838, 1056, 240, 29, false, L"... Open Settings"}, // 0034
    {241, 1084, 236, 28, false, L"... Change Screen Settings"}, // 0035
    {1594, 1230, 160, 28, false, L": Narrower"}, // 0036
    {1756, 1230, 160, 28, false, L": Wider"}, // 0037
    {3472, 1230, 160, 28, false, L": Shorter"}, // 0038
    {3634, 1230, 160, 28, false, L": Taller"}, // 0039
    {605, 1021, 274, 28, false, L": Integer Scale"}, // 0040
    {963, 1028, 264, 28, false, L"... Set Player Side"}, // 0041
    {2888, 1041, 252, 29, false, L"... Controller Settings"}, // 0042
    {1926, 887, 458, 25, false, L"Use the controller you want to assign."}, // 0043
    {3644, 938, 370, 25, false, L"Rapid fire is unavailable for this control."}, // 0074
    {3246, 877, 524, 23, false, L"Changing screen settings turns Gadgets off."}, // 0075
    {1, 1023, 264, 29, false, L"Screen Size Mode"}, // 0076
    {963, 871, 636, 21, false, L"Left: narrower    Right: wider    Down: shorter    Up: taller"}, // 0077
    {1553, 1140, 224, 28, false, L"DIP Difficulty"}, // 0078
    {449, 1173, 200, 28, false, L"Starting Lives"}, // 0079
    {2730, 727, 150, 28, false, L"Starting Weapons"}, // 0080
    {1757, 1170, 166, 28, false, L"Extends"}, // 0081
    {3886, 599, 202, 29, false, L"Fixed Rank"}, // 0082
    {3246, 902, 396, 28, false, L"Arcade Special Modes"}, // 0084
    {2386, 1164, 202, 28, false, L"Enemy Durability"}, // 0085
    {663, 1170, 202, 28, false, L"Bullet Density"}, // 0086
    {1243, 1113, 228, 28, false, L"Bullet Speed"}, // 0087
    {1926, 1118, 226, 28, false, L"Mad Ball Random Attacks"}, // 0088
    {605, 961, 316, 27, false, L"Slayer Item Drops"}, // 0089
    {1553, 1170, 202, 28, false, L"Small Weapon Value"}, // 0090
    {963, 1058, 240, 29, false, L"Auto Weapon Threshold"}, // 0091
    {1243, 991, 280, 29, false, L"Item Fall Speed"}, // 0092
    {1, 992, 280, 29, false, L"Medal Magnet"}, // 0093
    {2386, 988, 290, 29, false, L"Missed Medal Penalty"}, // 0094
    {1553, 1046, 256, 28, false, L"Weapon Invulnerability"}, // 0095
    {2130, 1148, 200, 29, false, L"Ship Hitbox"}, // 0096
    {3912, 1026, 174, 28, false, L"Anti-Milking"}, // 0097
    {2386, 957, 300, 29, false, L"Rank Increase (Time)"}, // 0098
    {1, 1114, 228, 28, false, L"Weapon Scoring"}, // 0099
    {2128, 1179, 200, 27, false, L"Rapid Fire Rate"}, // 0100
    {3918, 259, 176, 28, false, L"Shot Power"}, // 0101
    {1243, 960, 300, 29, false, L"Rank Increase (Overall)"}, // 0102
    {1, 961, 300, 29, false, L"Rank Increase (Actions)"}, // 0103
    {605, 1051, 252, 28, false, L"Hidden Ships"}, // 0104
    {1243, 1173, 200, 28, false, L"Quick Restart"}, // 0105
    {3246, 1111, 232, 28, false, L"Free Play"}, // 0106
    {3918, 289, 176, 28, false, L"Bullet Color"}, // 0107
    {3480, 1111, 230, 28, false, L"Rapid Fire Cap"}, // 0108
    {3790, 809, 304, 29, false, L"Attract Sound"}, // 0109
    {1781, 1139, 138, 28, false, L"Continues"}, // 0110
    {2154, 1118, 226, 28, false, L"Auto-Submit Score"}, // 0111
    {1553, 1016, 278, 28, false, L"Auto-Save Replay"}, // 0112
    {3080, 1188, 164, 29, false, L"Delay Reduction"}, // 0113
    {3246, 1171, 202, 28, false, L"Screen Size Mode"}, // 0114
    {3922, 995, 168, 29, false, L"Rotation"}, // 0115
    {1, 1144, 202, 29, false, L"Screen Size"}, // 0116
    {3690, 1141, 138, 28, false, L"Smoothing"}, // 0117
    {2622, 1106, 164, 28, false, L"Scanlines"}, // 0118
    {1743, 1200, 174, 28, false, L"Wallpaper"}, // 0119
    {2888, 1188, 190, 28, false, L"BGM Volume"}, // 0120
    {3838, 1147, 202, 29, false, L"SFX Volume"}, // 0121
    {509, 1054, 92, 28, false, L"EQ"}, // 0122
    {303, 961, 300, 29, false, L"TOTAL SCORE (LARGE)"}, // 0124
    {1926, 914, 380, 28, false, L"INSTRUCTION CARD (LEFT)"}, // 0136
    {1365, 894, 398, 28, false, L"INSTRUCTION CARD (RIGHT)"}, // 0137
    {605, 990, 282, 29, false, L"RANK GRAPH (SMALL)"}, // 0140
    {1553, 924, 342, 29, false, L"FORMATION INFO (SMALL)"}, // 0142
    {2386, 887, 400, 28, false, L"NEW INSTRUCTION CARD (LEFT)"}, // 0143
    {963, 894, 400, 28, false, L"NEW INSTRUCTION CARD (RIGHT)"}, // 0144
    {3918, 319, 176, 28, false, L"Play Time"}, // 0145
    {2730, 846, 140, 29, false, L"Medals Collected"}, // 0146
    {3246, 1020, 268, 29, false, L"10,000-Point Medals"}, // 0147
    {1553, 955, 306, 29, false, L"10,000-Point Medals Missed"}, // 0148
    {3246, 1081, 240, 28, false, L"Small Weapon Items"}, // 0149
    {1243, 1083, 240, 28, false, L"Large Weapon Items"}, // 0150
    {231, 1142, 214, 29, false, L"Small Shot Power-Ups"}, // 0151
    {447, 1142, 214, 29, false, L"Large Shot Power-Ups"}, // 0152
    {3950, 965, 140, 28, false, L"Options Collected"}, // 0153
    {2888, 1011, 280, 28, false, L"Extend Items"}, // 0154
    {3246, 1051, 252, 28, false, L"Score Extends"}, // 0155
    {3080, 1219, 164, 28, false, L"Shots Fired"}, // 0156
    {3918, 135, 176, 29, false, L"Weapons Fired"}, // 0157
    {1243, 1053, 252, 28, false, L"Formation Changes"}, // 0158
    {1, 1054, 252, 28, false, L"Special Power-Ups"}, // 0159
    {2386, 1019, 278, 28, false, L"Special Formations"}, // 0160
    {1, 1175, 200, 28, false, L"Total Score"}, // 0161
    {963, 1119, 226, 28, false, L"Ship Selections"}, // 0162
    {1861, 955, 62, 28, false, L"Ship 1"}, // 0163
    {2814, 887, 68, 28, false, L"Ship 2"}, // 0164
    {2814, 917, 68, 28, false, L"Ship 3"}, // 0165
    {2814, 947, 68, 28, false, L"Ship 4"}, // 0166
    {4042, 1147, 48, 27, false, L"Gain"}, // 0167
    {3592, 932, 50, 28, false, L"Chitta"}, // 0168
    {3142, 1041, 100, 27, false, L"Miyamoto"}, // 0169
    {1191, 1119, 50, 27, false, L"Bornnam"}, // 0170
    {1409, 1236, 138, 27, false, L"Bosses Defeated"}, // 0171
    {3134, 1072, 110, 28, false, L"Nose Laughing"}, // 0172
    {1461, 1143, 86, 27, false, L"Mad Ball"}, // 0173
    {1, 1205, 190, 27, false, L"Earth Crisis"}, // 0174
    {1926, 1237, 138, 27, false, L"Satanic Surfer"}, // 0175
    {1811, 1046, 112, 28, false, L"Black Heart"}, // 0176
    {847, 1081, 112, 28, false, L"Junkie Monkey"}, // 0177
    {3124, 1102, 112, 28, false, L"Black Heart"}, // 0178
    {3918, 349, 176, 28, false, L"Nose Laughing KAI"}, // 0179
    {2730, 817, 150, 27, false, L"Mad Ball KAI"}, // 0180
    {859, 1051, 100, 27, false, L"Slayer"}, // 0181
    {1445, 1173, 100, 27, false, L"Fortress"}, // 0182
    {2386, 1194, 188, 28, false, L"Black Heart Mk II"}, // 0183
    {2386, 1224, 164, 28, false, L"Glow Squid"}, // 0184
    {1, 1084, 238, 28, false, L"Clears by Ship"}, // 0185
    {1553, 986, 300, 28, false, L"Clears by Mode"}, // 0187
    {3500, 1051, 140, 29, false, L"All Ships"}, // 0188
    {3538, 990, 102, 27, false, L"Arcade"}, // 0189
    {853, 1200, 100, 27, false, L"Premium"}, // 0190
    {1, 1262, 110, 27, false, L"Super Easy"}, // 0191
    {313, 1287, 74, 27, false, L"Custom"}, // 0192
    {1553, 1109, 226, 29, false, L"Yakitori Triggers"}, // 0193
    {2888, 1132, 226, 28, false, L"Yakitori Roasts"}, // 0194
    {3246, 809, 542, 29, false, L"Black Heart Winder Survival (No Miss, No Weapon)"}, // 0195
    {3416, 636, 558, 29, false, L"Black Heart and Black Heart Mk II outside Arcade"}, // 0196
    {3662, 1200, 174, 28, false, L"Deaths"}, // 0197
    {3838, 1261, 112, 28, false, L"Suicides"}, // 0198
    {2128, 1208, 188, 27, false, L"EVERY 1 MILLION"}, // 0221
    {2386, 1049, 264, 27, false, L"1 AND 2 MILLION ONLY"}, // 0222
    {3918, 379, 176, 28, false, L"Before Ship Select"}, // 0225
    {3918, 409, 176, 28, false, L"After Ship Select"}, // 0226
    {867, 1170, 92, 28, false, L"10/sec"}, // 0227
    {3376, 1259, 92, 28, false, L"12/sec"}, // 0228
    {1409, 1265, 92, 28, false, L"15/sec"}, // 0229
    {3992, 483, 100, 28, false, L"20/sec"}, // 0230
    {3838, 1178, 200, 28, false, L"Horizontal Fill"}, // 0231
    {3918, 228, 174, 29, false, L"Horizontal Sharp"}, // 0232
    {3246, 990, 290, 28, false, L"Horizontal CRT"}, // 0233
    {1926, 972, 302, 28, false, L"Vertical Fill (Left)"}, // 0234
    {283, 992, 276, 29, false, L"Vertical Sharp (Left)"}, // 0235
    {3246, 932, 344, 28, false, L"Vertical CRT (Left)"}, // 0236
    {2730, 661, 154, 31, false, L"Vertical Fill"}, // 0238
    {2730, 694, 154, 31, false, L"Vertical Sharp"}, // 0239
    {1553, 1076, 220, 31, false, L"Vertical CRT"}, // 0240
    {4048, 769, 44, 26, false, L"0\u00b0"}, // 0241
    {3246, 1141, 224, 28, false, L"90\u00b0 (Left)"}, // 0242
    {1594, 1288, 74, 26, false, L"180\u00b0"}, // 0243
    {2888, 1072, 244, 28, false, L"270\u00b0 (Right)"}, // 0244
    {1497, 1053, 50, 28, false, L"Sharp"}, // 0245
    {2308, 914, 76, 28, false, L"Fill"}, // 0246
    {3992, 513, 100, 28, false, L"Integer"}, // 0247
    {1846, 1260, 76, 27, false, L"Manual"}, // 0248
    {2666, 1019, 120, 27, false, L"L 0 / S 20"}, // 0253
    {1287, 1266, 92, 27, false, L"L 1 / S 0"}, // 0254
    {113, 1262, 100, 27, false, L"L 2 / S 0"}, // 0255
    {663, 1263, 100, 27, false, L"L 3 / S 0"}, // 0256
    {765, 1263, 100, 27, false, L"L 4 / S 0"}, // 0257
    {2668, 917, 116, 27, false, L"L 4 / S 40"}, // 0258
    {963, 1149, 200, 29, false, L"Horizontal Layout"}, // 0264
    {1926, 1148, 202, 29, false, L"Vertical Layout"}, // 0265
    {4006, 840, 88, 29, false, L"ED Layout"}, // 0266
    {3992, 453, 102, 28, false, L"Home Theater"}, // 0269
    {881, 1021, 76, 28, false, L"Arcade"}, // 0270
    {3598, 1283, 76, 27, false, L"Live"}, // 0271
    {3992, 543, 100, 28, false, L"Rapid Fire"}, // 0272
    {3344, 644, 56, 21, false, L"Default"}, // 0273
    {467, 1287, 92, 21, false, L"Joystick 1"}, // 0274
    {3992, 573, 96, 21, false, L"Joystick 2"}, // 0275
    {3748, 1260, 86, 27, false, L"Large Weapon"}, // 0295
    {2688, 957, 94, 28, false, L"Fixed at 0%"}, // 0296
    {1243, 1143, 216, 28, false, L"Fixed by Stage"}, // 0297
    {3918, 166, 176, 29, false, L"Max Rank"}, // 0298
    {3918, 197, 176, 29, false, L"Min Rank"}, // 0299
    {255, 1054, 252, 28, false, L"Super Easy"}, // 0300
    {963, 1180, 192, 28, false, L"Premium"}, // 0301
    {2652, 1049, 134, 28, false, L"OFF (+1)"}, // 0302
    {2204, 1002, 180, 28, false, L"Add 1 (+2)"}, // 0303
    {2590, 1195, 188, 28, false, L"Add 2 (+3)"}, // 0304
    {663, 1200, 188, 28, false, L"Add 3 (+4)"}, // 0305
    {1553, 1200, 188, 28, false, L"Add 4 (+5)"}, // 0306
    {989, 1236, 138, 28, false, L"1+ Large"}, // 0307
    {1129, 1236, 138, 28, false, L"1+ Small"}, // 0308
    {963, 1089, 236, 28, false, L"1+ Large + Shot"}, // 0309
    {1926, 1179, 200, 28, false, L"Varies With Rank"}, // 0310
    {267, 1023, 264, 29, false, L"Min Rank Speed"}, // 0311
    {3644, 995, 276, 29, false, L"75% of Min Rank Speed"}, // 0312
    {963, 997, 276, 29, false, L"50% of Min Rank Speed"}, // 0313
    {1926, 1002, 276, 29, false, L"33% of Min Rank Speed"}, // 0314
    {3952, 1261, 112, 28, false, L"Min Level"}, // 0315
    {2242, 1237, 132, 28, false, L"Down 5 Levels"}, // 0316
    {2888, 1246, 132, 28, false, L"Down 3 Levels"}, // 0317
    {833, 1111, 128, 28, false, L"Half Size"}, // 0318
    {2230, 972, 140, 28, false, L"Smallest"}, // 0319
    {1765, 894, 142, 28, false, L"Fixed 15/sec"}, // 0323
    {2888, 1102, 234, 28, false, L"Fixed Shot Lv. 4"}, // 0324
    {2386, 1106, 234, 28, false, L"Fixed Shot Lv. 5"}, // 0325
    {605, 1111, 226, 29, false, L"Weapon Kill Points"}, // 0326
    {3472, 1141, 216, 29, false, L"Min Rank Durability"}, // 0327
    {3976, 636, 114, 29, false, L"No Flash"}, // 0328
    {3970, 902, 124, 28, false, L"Flash (Bright)"}, // 0329
    {2730, 757, 150, 28, false, L"Flash (Dark)"}, // 0330
    {2590, 1164, 188, 29, false, L"Flash (Pink)"}, // 0331
    {3472, 1200, 188, 28, false, L"Flash (Blue)"}, // 0332
    {231, 1201, 188, 28, false, L"Flash (Yellow)"}, // 0333
    {3246, 1201, 188, 28, false, L"Flash (Green)"}, // 0334
    {1781, 1109, 140, 28, false, L"Sound On"}, // 0335
    {1775, 1076, 140, 29, false, L"Sound Off"}, // 0336
    {1269, 1236, 138, 28, false, L"Allow Continues"}, // 0337
    {3690, 1171, 140, 27, false, L"No Continues"}, // 0338
    {2332, 1148, 48, 27, false, L"Random"}, // 0339
};

inline constexpr GareggaCaption kRanking[]{
    {1751, 403, 86, 28, false, L"... OK"}, // 0054
    {1751, 433, 86, 27, false, L"... Back"}, // 0055
    {1, 1351, 252, 28, false, L"... Download Replay"}, // 0056
    {1545, 1413, 200, 28, false, L"... Mode Select"}, // 0057
    {1747, 1413, 200, 28, false, L"... Details"}, // 0058
    {1013, 1406, 210, 27, false, L"... My Rank"}, // 0059
    {1225, 1406, 188, 27, false, L"... Replay DL"}, // 0060
    {1715, 358, 112, 27, false, L"... Profile"}, // 0061
    {1, 1418, 200, 28, false, L"... Play Replay"}, // 0062
    {1, 1381, 240, 28, false, L"... Filter"}, // 0063
    {1783, 1383, 214, 28, false, L"... Top 10"}, // 0064
    {635, 1390, 206, 19, true, L"Choose a ranking to view."}, // 0070
    {757, 1418, 190, 19, true, L"Set the ranking filter."}, // 0071
    {1975, 329, 62, 28, false, L"Ship 1"}, // 0072
    {1975, 239, 66, 28, false, L"Ship 2"}, // 0073
    {1975, 269, 66, 28, false, L"Ship 3"}, // 0074
    {1975, 299, 66, 28, false, L"Ship 4"}, // 0075
    {1975, 618, 48, 27, false, L"Gain"}, // 0076
    {1975, 500, 50, 28, false, L"Chitta"}, // 0077
    {1939, 29, 100, 27, false, L"Miyamoto"}, // 0078
    {1975, 560, 50, 27, false, L"Bornnam"}, // 0079
    {829, 1351, 160, 29, false, L"All Ships"}, // 0080
    {1715, 327, 120, 29, false, L"All Ships"}, // 0081
    {1975, 359, 62, 28, false, L"Ship 1"}, // 0082
    {1975, 65, 68, 28, false, L"Ship 2"}, // 0083
    {1975, 95, 68, 28, false, L"Ship 3"}, // 0084
    {1975, 125, 68, 28, false, L"Ship 4"}, // 0085
    {1975, 647, 48, 27, false, L"Gain"}, // 0086
    {1975, 530, 50, 28, false, L"Chitta"}, // 0087
    {911, 1175, 100, 27, false, L"Miyamoto"}, // 0088
    {1975, 589, 50, 27, false, L"Bornnam"}, // 0089
};

inline constexpr GareggaCaption kReplay[]{
    {1111, 1090, 86, 28, false, L"... OK"}, // 0050
    {1201, 1097, 86, 27, false, L"... Back"}, // 0051
    {615, 1013, 200, 28, false, L"... Play Replay"}, // 0052
    {411, 1043, 200, 28, false, L"... Filter"}, // 0053
    {1669, 230, 276, 27, false, L"... Lock Replay"}, // 0054
    {1669, 259, 276, 27, false, L"... Delete Replay"}, // 0055
    {411, 1013, 202, 28, false, L"... Save Replay"}, // 0056
    {1669, 288, 262, 28, false, L"... Stop Saving"}, // 0057
    {1289, 1097, 70, 28, false, L": Play"}, // 0058
    {1, 1079, 122, 28, false, L": Stop"}, // 0059
    {1863, 398, 184, 29, false, L": Move Display"}, // 0060
    {613, 1072, 140, 28, false, L": Back 10 sec"}, // 0061
    {1545, 1061, 160, 28, false, L": Step Frame"}, // 0062
    {1013, 1090, 96, 27, false, L": Rewind"}, // 0063
    {1013, 1061, 186, 27, false, L": Lock Replay"}, // 0064
    {411, 1073, 136, 27, false, L"Hold"}, // 0065
    {1669, 207, 354, 21, true, L"Select a replay filter."}, // 0076
    {549, 1073, 62, 28, false, L"Ship 1"}, // 0077
    {1361, 1097, 66, 28, false, L"Ship 2"}, // 0078
    {1429, 1097, 66, 28, false, L"Ship 3"}, // 0079
    {1497, 1097, 66, 28, false, L"Ship 4"}, // 0080
    {1887, 1097, 48, 27, false, L"Gain"}, // 0081
    {1627, 1097, 50, 28, false, L"Chitta"}, // 0082
    {125, 1079, 100, 27, false, L"Miyamoto"}, // 0083
    {1679, 1097, 50, 27, false, L"Bornnam"}, // 0084
    {237, 1055, 160, 29, false, L"All Ships"}, // 0085
    {817, 1053, 190, 27, false, L"Friends' Replays"}, // 0086
    {1967, 177, 74, 27, false, L"Trophy"}, // 0087
    {1731, 1097, 50, 27, false, L"Locked"}, // 0088
    {1783, 1097, 50, 27, false, L"Unlocked"}, // 0089
    {1379, 1061, 164, 28, false, L"Newest First"}, // 0090
    {1201, 1061, 176, 28, false, L"Highest Score"}, // 0091
    {1835, 1097, 50, 27, false, L"Locked"}, // 0094
    {1947, 294, 76, 16, false, L"Saved Slot"}, // 0104
    {1986, 1118, 48, 16, false, L"Empty Slot"}, // 0105
    {1813, 1031, 172, 16, false, L"Sort by newest replay."}, // 0106
    {237, 1086, 172, 16, false, L"Sort by highest score."}, // 0107
};

inline constexpr GareggaCaption kArchive0[]{
    {853, 1913, 24, 28, false, L"x"}, // 0012
    {317, 1971, 24, 27, false, L"x"}, // 0013
    {1237, 1856, 26, 28, false, L"x"}, // 0014
    {2025, 1, 22, 27, false, L"x"}, // 0015
    {2025, 30, 22, 27, false, L"x"}, // 0016
    {1811, 2005, 24, 27, false, L"x"}, // 0017
    {585, 1888, 26, 27, false, L"x"}, // 0021
    {1993, 1641, 42, 19, false, L"0k pts"}, // 0024
    {345, 1888, 238, 28, false, L"Unlimited / Free Play"}, // 0029
    {1547, 1851, 86, 28, false, L"... OK"}, // 0030
    {241, 1913, 86, 27, false, L"... Back"}, // 0031
    {345, 1948, 224, 27, false, L": View"}, // 0032
    {1635, 1857, 276, 27, false, L": Archive Room"}, // 0033
    {777, 1681, 134, 28, false, L": Next"}, // 0035
    {1913, 1857, 134, 28, false, L": Move Image"}, // 0036
    {841, 1621, 70, 28, false, L": Zoom In"}, // 0037
    {705, 1711, 200, 31, false, L": Play / Pause"}, // 0039
    {1885, 1423, 158, 29, false, L": Stop Sound"}, // 0040
    {241, 1971, 74, 27, false, L"Hold"}, // 0041
    {2025, 59, 22, 27, false, L"Key"}, // 0042
    {1, 2001, 184, 28, false, L": Hide Window"}, // 0044
    {727, 2002, 160, 28, false, L": Reset Zoom"}, // 0045
    {1895, 1454, 150, 28, false, L": Fade Out"}, // 0047
    {1867, 1515, 176, 28, false, L"Time Running"}, // 0060
    {889, 1999, 202, 27, false, L"Play Time"}, // 0061
    {1117, 1914, 140, 29, false, L"Medals Collected"}, // 0062
    {345, 1857, 268, 29, false, L"10,000-Point Medals"}, // 0063
    {345, 1771, 306, 29, false, L"10,000-Point Medals Missed"}, // 0064
    {1797, 1241, 240, 28, false, L"Small Weapon Items"}, // 0065
    {1797, 1271, 240, 28, false, L"Large Weapon Items"}, // 0066
    {615, 1943, 214, 29, false, L"Small Shot Power-Ups"}, // 0067
    {1635, 1944, 214, 29, false, L"Large Shot Power-Ups"}, // 0068
    {1493, 1941, 140, 28, false, L"Options Collected"}, // 0069
    {1265, 1851, 280, 28, false, L"Extend Items"}, // 0070
    {1779, 1571, 252, 28, false, L"Score Extends"}, // 0071
    {1093, 1999, 164, 28, false, L"Shots Fired"}, // 0072
    {1869, 1484, 176, 29, false, L"Weapons Fired"}, // 0073
    {653, 1771, 252, 28, false, L"Formation Changes"}, // 0074
    {1265, 1881, 252, 28, false, L"Special Power-Ups"}, // 0075
    {957, 1856, 278, 28, false, L"Special Formations"}, // 0076
    {1635, 1975, 200, 28, false, L"Total Score"}, // 0077
    {345, 1918, 226, 28, false, L"Ship Selections"}, // 0078
    {281, 1829, 62, 28, false, L"Ship 1"}, // 0079
    {843, 1573, 68, 28, false, L"Ship 2"}, // 0080
    {1987, 1303, 50, 28, false, L"Chitta"}, // 0084
    {795, 1651, 100, 27, false, L"Miyamoto"}, // 0085
    {897, 1857, 50, 27, false, L"Bornnam"}, // 0086
    {1489, 1971, 138, 27, false, L"Bosses Defeated"}, // 0087
    {1929, 1132, 110, 28, false, L"Nose Laughing"}, // 0088
    {241, 1942, 86, 27, false, L"Mad Ball"}, // 0089
    {345, 2002, 190, 27, false, L"Earth Crisis"}, // 0090
    {1929, 1013, 112, 28, false, L"Black Heart"}, // 0092
    {1929, 1043, 112, 28, false, L"Junkie Monkey"}, // 0093
    {1869, 1914, 176, 28, false, L"Nose Laughing KAI"}, // 0094
    {187, 2001, 150, 27, false, L"Mad Ball KAI"}, // 0095
    {1925, 1721, 100, 27, false, L"Slayer"}, // 0096
    {1925, 1750, 100, 27, false, L"Fortress"}, // 0097
    {1851, 1944, 188, 28, false, L"Black Heart Mk II"}, // 0098
    {1265, 1911, 238, 28, false, L"Clears by Ship"}, // 0100
    {655, 1827, 300, 28, false, L"Clears by Mode"}, // 0101
    {1941, 1333, 102, 27, false, L"Arcade"}, // 0102
    {1925, 1779, 100, 27, false, L"Premium"}, // 0103
    {1929, 1162, 110, 27, false, L"Super Easy"}, // 0104
    {889, 1914, 226, 29, false, L"Yakitori Triggers"}, // 0106
    {1265, 1941, 226, 28, false, L"Yakitori Roasts"}, // 0107
    {1325, 1484, 542, 29, false, L"Black Heart Winder Survival (No Miss, No Weapon)"}, // 0108
    {1325, 1423, 558, 29, false, L"Black Heart and Black Heart Mk II outside Arcade"}, // 0109
    {1635, 2005, 174, 28, false, L"Deaths"}, // 0110
    {1929, 1073, 112, 28, false, L"Suicides"}, // 0111
    {1, 1829, 278, 29, false, L"Glow Squid (Final Form)"}, // 0112
    {345, 1827, 308, 28, false, L"Black Heart Mk II (Arcade)"}, // 0113
    {1, 1913, 238, 28, false, L"Unlocked Credits"}, // 0114
    {831, 1943, 50, 27, false, L"Bornnam"}, // 0120
    {1325, 1631, 420, 27, false, L"Battle Garegga Artwork"}, // 0121
    {353, 1741, 344, 28, false, L"Ship 1 Artwork"}, // 0122
    {1, 1711, 350, 28, false, L"Ship 2 Artwork"}, // 0123
    {353, 1711, 350, 28, false, L"Ship 3 Artwork"}, // 0124
    {1, 1741, 350, 28, false, L"Ship 4 Artwork"}, // 0125
    {399, 1651, 394, 28, false, L"Black Heart Artwork"}, // 0126
    {1, 1771, 342, 27, false, L"Battle Garegga Instruction Card"}, // 0127
};

inline constexpr GareggaCaption kArchive1[]{
    {981, 60, 24, 27, false, L"wk"}, // 0018
    {67, 67, 44, 27, false, L"x"}, // 0019
    {113, 67, 44, 27, false, L"x"}, // 0020
    {343, 67, 24, 27, false, L"x"}, // 0022
    {483, 38, 134, 27, false, L": Previous"}, // 0034
    {253, 39, 70, 28, false, L": Zoom Out"}, // 0038
    {1, 9, 174, 27, false, L"... Fast Scroll"}, // 0043
    {839, 60, 70, 27, false, L": Back"}, // 0046
    {679, 9, 158, 28, false, L": Show Window"}, // 0048
    {679, 39, 68, 28, false, L"Ship 3"}, // 0081
    {749, 39, 68, 28, false, L"Ship 4"}, // 0082
    {619, 38, 48, 27, false, L"Gain"}, // 0083
    {343, 38, 138, 27, false, L"Satanic Surfer"}, // 0091
    {177, 9, 164, 28, false, L"Glow Squid"}, // 0099
    {177, 39, 74, 27, false, L"Custom"}, // 0105
};

inline constexpr GareggaCaption kReplayGadget[]{
    {1388, 2521, 148, 24, true, L"Pause"}, // 0395
    {1388, 2553, 148, 24, true, L"Stop Replay"}, // 0395
    {2351, 2524, 148, 24, true, L"Play"}, // 0396
    {2351, 2556, 148, 24, true, L"Stop Replay"}, // 0396
    {2673, 2509, 148, 24, true, L"Rewind"}, // 0397
    {2673, 2541, 148, 24, true, L"Rewind"}, // 0397
    {2673, 2573, 148, 24, true, L"Slower"}, // 0397
    {2935, 2509, 148, 24, true, L"Fast Forward"}, // 0397
    {2935, 2541, 148, 24, true, L"Fast Forward"}, // 0397
    {2935, 2573, 148, 24, true, L"Faster"}, // 0397
    {65, 2513, 148, 24, true, L"Rewind"}, // 0398
    {65, 2545, 148, 24, true, L"Rewind"}, // 0398
    {65, 2577, 148, 24, true, L"Unused"}, // 0398
    {327, 2513, 148, 24, true, L"Fast Forward"}, // 0398
    {327, 2545, 148, 24, true, L"Fast Forward"}, // 0398
    {327, 2577, 148, 24, true, L"Step Frame"}, // 0398
};

inline constexpr GareggaAtlas kEnglishAtlases[]{
    {"2000/motion/title_pause_menu.psb.m",
        "9d0172e2bf4bbecf667fcdacee745de82ff33697df9cc76cebc7648c52e2acb7", 0x198e0, 1024, 1024, 4, kPause},
    {"2000/motion/title_ui_common.psb.m",
        "35cd41557d9090a64a88ecd5fa643e034d0c51816ef7b8295082537cc3d40218", 0x27600, 4096, 2048, 4, kCommon},
    {"2000/motion/title_ui_dialog.psb.m",
        "155d447094e9585c81457e37293c1b264b1f294e3ee8983adbbbd8fe4fac262c", 0xad00, 2048, 2048, 4, kDialog},
    {"2000/motion/title_ui_option.psb.m",
        "c8498246db321589b154796a28023eccfc7f3e005089ac8e5e955a110d30a6e7", 0x62540, 4096, 2048, 4, kOptions},
    {"2000/motion/title_ui_ranking.psb.m",
        "e1f37b8e68829979ed36e004c9b4087434f8b54f5eb1a6689ed197fffc8b13e4", 0x21ea0, 2048, 2048, 4, kRanking},
    {"2000/motion/title_ui_replay.psb.m",
        "eba0a11297464f0a1a68d2ae2bcd21728688c089cc6886b2caa79176c08b2ec2", 0x19f60, 2048, 2048, 4, kReplay},
    {"2000/motion/title_ui_materials.psb.m",
        "15869260544b034cada3c5eb3b14497b1aade3e6ca6aeb3efd7d68aa53616832", 0x5f280, 2048, 2048, 2, kArchive0},
    {"2000/motion/title_ui_materials.psb.m",
        "15869260544b034cada3c5eb3b14497b1aade3e6ca6aeb3efd7d68aa53616832", 0x1f280, 1024, 128, 2, kArchive1},
    {"2000/motion/title_gadget.psb.m",
        "dc18b68389906f6746bcce4cdee0c3e6daa6481d70765f7ab5e2c9ce4e6fceb3", 0x4c560, 4096, 4096, 4, kReplayGadget, true},
};
}
