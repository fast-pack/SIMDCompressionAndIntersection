/**
 * This code is released under the
 * Apache License Version 2.0 http://www.apache.org/licenses/.
 *
 */

#include "binarypacking.h"
#include "bitpackinghelpers.h"
#include "codecfactory.h"
#include "common.h"
#include "compositecodec.h"
#include "delta.h"
#include "simdbinarypacking.h"
#include "simdbitpackinghelpers.h"
#include "simdfastpfor.h"
#include "synthetic.h"
#include "util.h"
#include "variablebyte.h"

using namespace std;
using namespace SIMDCompressionLib;

// taken from https://github.com/lemire/SIMDCompressionAndIntersection/issues/18
const uint32_t somearray[1931] = {
    2512,    2513,    2515,    1620632, 1620655, 3468318, 3468319, 3468320,
    3468321, 3468322, 3468323, 3468325, 3468338, 3468339, 3468340, 3468341,
    3468342, 3468343, 3468344, 3468346, 3468347, 3468348, 3468349, 3468350,
    3468351, 3468353, 3468354, 3468355, 3468356, 3468357, 3468359, 3468360,
    3468361, 3468362, 3468363, 3468364, 3468365, 3468366, 3468367, 3468368,
    3468369, 3468370, 3468371, 3468373, 3468375, 3468376, 3468377, 3468378,
    3468379, 3468380, 3468381, 3468382, 3468383, 3468384, 3468385, 3468386,
    3468387, 3468388, 3468389, 3468390, 3468391, 3468393, 3468394, 3468395,
    3468396, 3468397, 3468398, 3468399, 3468400, 3468401, 3468402, 3468403,
    3468404, 3468405, 3468407, 3468408, 3468409, 3468410, 3468411, 3468413,
    3468414, 3468415, 3468416, 3468417, 3468418, 3468419, 3468421, 3468436,
    3468458, 3468479, 3468483, 3468484, 3468489, 3468516, 3468517, 3468518,
    3468519, 3468520, 3468521, 3468542, 3468543, 3468544, 3468545, 3468546,
    3468547, 3468548, 3468553, 3468554, 3468555, 3468556, 3468557, 3468558,
    3468559, 3468650, 3468651, 3468652, 3468653, 3468654, 3468655, 3468656,
    3468658, 3468703, 3468995, 3477194, 3477195, 3477196, 3477198, 3477301,
    3477451, 3477452, 3477461, 3477462, 3477463, 3477464, 3477737, 3477739,
    3477740, 3477811, 3477812, 3477813, 3477856, 3477933, 3477966, 3477981,
    3477982, 3477985, 3477986, 3477987, 3477991, 3478151, 3478152, 3478156,
    3478273, 3478276, 3478279, 3478342, 3478343, 3478345, 3478346, 3478349,
    3478350, 3478351, 3478352, 3478354, 3478355, 3478357, 3478363, 3478376,
    3478381, 3478384, 3478400, 3478404, 3478406, 3478418, 3478419, 3478425,
    3478429, 3478431, 3478482, 3478483, 3478498, 3478501, 3478540, 3478547,
    3478562, 3479440, 3479441, 3479442, 3479443, 3479444, 3479445, 3479446,
    3479447, 3479448, 3479449, 3479575, 3479576, 3479577, 3479578, 3479579,
    3479580, 3479581, 3479582, 3479583, 3479584, 3479585, 3479586, 3479587,
    3479588, 3479589, 3479590, 3479591, 3479592, 3479593, 3479594, 3479595,
    3479596, 3479597, 3479598, 3479599, 3479600, 3479601, 3479602, 3479603,
    3479604, 3479605, 3479606, 3479607, 3479608, 3479609, 3479610, 3479611,
    3479612, 3479613, 3479614, 3479615, 3479616, 3479617, 3479618, 3479619,
    3479620, 3479621, 3479624, 3479625, 3479626, 3479627, 3479628, 3479629,
    3479630, 3479631, 3479632, 3479633, 3479683, 3479789, 3479790, 3479791,
    3479792, 3479793, 3479794, 3479795, 3479796, 3479797, 3479798, 3479799,
    3479800, 3479801, 3480246, 3480441, 3480482, 3480816, 3480969, 3480986,
    3481019, 3481215, 3481723, 3482066, 3482344, 3482419, 3482695, 3482807,
    3482825, 3482842, 3482843, 3482848, 3482902, 3482944, 3482956, 3483219,
    3483226, 3483249, 3483300, 3483352, 3483689, 3483936, 3483954, 3484123,
    3484787, 3484837, 3484877, 3485189, 3485239, 3485240, 3485241, 3485246,
    3485250, 3485251, 3485255, 3485256, 3485258, 3485261, 3485262, 3485263,
    3485291, 3485294, 3485300, 3485386, 3485410, 3485414, 3485415, 3485416,
    3485419, 3485544, 3485545, 3485547, 3485548, 3485550, 3485551, 3485773,
    3485775, 3485776, 3485907, 3485912, 3485913, 3485915, 3485917, 3485921,
    3485922, 3485926, 3485931, 3485956, 3486144, 3486147, 3486149, 3486151,
    3486152, 3486155, 3486156, 3486160, 3486161, 3486162, 3486167, 3486168,
    3486173, 3486174, 3486175, 3486176, 3486183, 3486184, 3486189, 3486194,
    3486205, 3486208, 3486430, 3486804, 3486807, 3486808, 3486809, 3486810,
    3486811, 3486812, 3486813, 3486814, 3486815, 3486816, 3486817, 3486931,
    3486932, 3486933, 3486934, 3486935, 3487273, 3487274, 3487275, 3487277,
    3487379, 3487383, 3487389, 3487393, 3487395, 3487396, 3487397, 3487399,
    3487401, 3487402, 3487409, 3487411, 3487416, 3487417, 3487420, 3487523,
    3487528, 3487742, 3487743, 3487744, 3487745, 3487746, 3487748, 3487749,
    3487750, 3487751, 3487752, 3487753, 3487759, 3487760, 3487761, 3487762,
    3487763, 3487764, 3487765, 3487767, 3487768, 3487769, 3487770, 3487771,
    3487772, 3487773, 3487774, 3487775, 3487776, 3487777, 3487778, 3487779,
    3487780, 3487782, 3487783, 3487785, 3487789, 3487790, 3487855, 3487903,
    3487904, 3487905, 3487906, 3487907, 3487908, 3487909, 3487911, 3487913,
    3487914, 3487916, 3487917, 3487918, 3487919, 3487920, 3487921, 3487922,
    3487924, 3487925, 3487927, 3487928, 3487931, 3487934, 3487935, 3487938,
    3487939, 3487940, 3487941, 3487943, 3487944, 3487947, 3487951, 3487952,
    3487953, 3487955, 3487957, 3487968, 3487969, 3487971, 3487972, 3487978,
    3487988, 3487991, 3487993, 3487997, 3487998, 3487999, 3488000, 3488002,
    3488003, 3488004, 3488005, 3488006, 3488009, 3488010, 3488011, 3488012,
    3488016, 3488017, 3488020, 3488024, 3488026, 3488027, 3488032, 3488034,
    3488037, 3488038, 3488040, 3488041, 3488048, 3488049, 3488051, 3488054,
    3488055, 3488056, 3488059, 3488062, 3488063, 3488065, 3488066, 3488067,
    3488069, 3488070, 3488072, 3488074, 3488076, 3488077, 3488079, 3488080,
    3488083, 3488084, 3488087, 3488088, 3488089, 3488091, 3488092, 3488093,
    3488095, 3488096, 3488098, 3488104, 3488107, 3488108, 3488112, 3488115,
    3488117, 3488118, 3488119, 3488122, 3488124, 3488126, 3488127, 3488130,
    3488132, 3488134, 3488135, 3488136, 3488139, 3488140, 3488141, 3488143,
    3488144, 3488145, 3488148, 3488149, 3488152, 3488153, 3488154, 3488155,
    3488156, 3488157, 3488160, 3488164, 3488168, 3488169, 3488172, 3488173,
    3488176, 3488180, 3488182, 3488183, 3488185, 3488186, 3488188, 3488189,
    3488195, 3488199, 3488200, 3488201, 3488205, 3488206, 3488210, 3488212,
    3488215, 3488217, 3488222, 3488227, 3488231, 3488233, 3488234, 3488235,
    3488237, 3488239, 3488242, 3488244, 3488247, 3488250, 3488251, 3488252,
    3488255, 3488256, 3488257, 3488259, 3488261, 3488264, 3488265, 3488266,
    3488267, 3488269, 3488270, 3488272, 3488274, 3488277, 3488278, 3488279,
    3488280, 3488283, 3488284, 3488286, 3488289, 3488290, 3488292, 3488293,
    3488298, 3488300, 3488302, 3488303, 3488307, 3488308, 3488309, 3488310,
    3488311, 3488314, 3488315, 3488317, 3488318, 3488319, 3488325, 3488326,
    3488327, 3488329, 3488330, 3488333, 3488335, 3488336, 3488337, 3488339,
    3488344, 3488345, 3488346, 3488348, 3488354, 3488355, 3488358, 3488359,
    3488364, 3488365, 3488370, 3488372, 3488374, 3488379, 3488382, 3488384,
    3488386, 3488389, 3488399, 3488400, 3488402, 3488403, 3488406, 3488408,
    3488413, 3488414, 3488416, 3488417, 3488418, 3488420, 3488423, 3488427,
    3488430, 3488433, 3488438, 3488439, 3488442, 3488443, 3488445, 3488447,
    3488448, 3488455, 3488458, 3488459, 3488461, 3488462, 3488464, 3488465,
    3488466, 3488467, 3488471, 3488472, 3488473, 3488476, 3488478, 3488481,
    3488483, 3488487, 3488488, 3488491, 3488492, 3488493, 3488494, 3488496,
    3488501, 3488503, 3488505, 3488506, 3488507, 3488508, 3488509, 3488510,
    3488511, 3488512, 3488513, 3488519, 3488522, 3488524, 3488526, 3488527,
    3488528, 3488530, 3488537, 3488538, 3488539, 3488541, 3488542, 3488544,
    3488547, 3488548, 3488552, 3488553, 3488554, 3488556, 3488557, 3488558,
    3488559, 3488560, 3488563, 3488566, 3488570, 3488571, 3488577, 3488580,
    3488586, 3488587, 3488589, 3488591, 3488593, 3488594, 3488596, 3488598,
    3488600, 3488601, 3488603, 3488606, 3488607, 3488609, 3488610, 3488617,
    3488619, 3488622, 3488625, 3488626, 3488627, 3488630, 3488633, 3488637,
    3488640, 3488644, 3488649, 3488651, 3488652, 3488655, 3488656, 3488657,
    3488661, 3488663, 3488665, 3488666, 3488668, 3488674, 3488676, 3488678,
    3488680, 3488685, 3488686, 3488687, 3488690, 3488694, 3488697, 3488698,
    3488699, 3488700, 3488701, 3488705, 3488708, 3488709, 3488710, 3488711,
    3488713, 3488714, 3488716, 3488717, 3488719, 3488720, 3488723, 3488725,
    3488727, 3488730, 3488731, 3488733, 3488734, 3488735, 3488736, 3488743,
    3488746, 3488747, 3488749, 3488753, 3488755, 3488756, 3488759, 3488760,
    3488761, 3488762, 3488763, 3488766, 3488767, 3488770, 3488773, 3488774,
    3488778, 3488780, 3488782, 3488783, 3488785, 3488787, 3488792, 3488793,
    3488794, 3488795, 3488796, 3488797, 3488798, 3488800, 3488803, 3488805,
    3488807, 3488809, 3488810, 3488813, 3488814, 3488815, 3488817, 3488819,
    3488820, 3488822, 3488824, 3488825, 3488826, 3488828, 3488830, 3488831,
    3488834, 3488836, 3488838, 3488840, 3488841, 3488842, 3488843, 3488844,
    3488845, 3488848, 3488850, 3488851, 3488859, 3488861, 3488862, 3488863,
    3488868, 3488869, 3488870, 3488871, 3488872, 3488874, 3488875, 3488877,
    3488878, 3488880, 3488883, 3488885, 3488886, 3488888, 3488892, 3488895,
    3488897, 3488898, 3488902, 3488904, 3488906, 3488909, 3488910, 3488911,
    3488913, 3488914, 3488915, 3488916, 3488917, 3488918, 3488919, 3488923,
    3488924, 3488925, 3488926, 3488927, 3488929, 3488930, 3488931, 3488932,
    3488934, 3488935, 3488937, 3488939, 3488940, 3488941, 3488942, 3488945,
    3488948, 3488950, 3488952, 3488953, 3488954, 3488958, 3488960, 3488961,
    3488962, 3488965, 3488968, 3488969, 3488971, 3488972, 3488975, 3488977,
    3488979, 3488980, 3488981, 3488987, 3488989, 3488990, 3488993, 3488996,
    3488997, 3488998, 3489001, 3489005, 3489006, 3489008, 3489012, 3489013,
    3489018, 3489021, 3489029, 3489032, 3489033, 3489036, 3489039, 3489042,
    3489043, 3489044, 3489049, 3489051, 3489052, 3489053, 3489055, 3489058,
    3489064, 3489066, 3489067, 3489068, 3489069, 3489070, 3489072, 3489074,
    3489076, 3489077, 3489079, 3489080, 3489082, 3489083, 3489084, 3489085,
    3489088, 3489093, 3489105, 3489106, 3489108, 3489111, 3489114, 3489115,
    3489119, 3489123, 3489124, 3489126, 3489130, 3489133, 3489134, 3489135,
    3489136, 3489137, 3489138, 3489141, 3489143, 3489149, 3489150, 3489152,
    3489154, 3489159, 3489161, 3489163, 3489165, 3489169, 3489175, 3489180,
    3489182, 3489184, 3489185, 3489191, 3489195, 3489197, 3489198, 3489201,
    3489202, 3489203, 3489208, 3489212, 3489213, 3489215, 3489217, 3489218,
    3489219, 3489221, 3489223, 3489230, 3489232, 3489238, 3489239, 3489243,
    3489245, 3489248, 3489249, 3489250, 3489251, 3489256, 3489257, 3489258,
    3489260, 3489264, 3489266, 3489268, 3489277, 3489278, 3489282, 3489283,
    3489286, 3489289, 3489290, 3489300, 3489301, 3489303, 3489304, 3489306,
    3489309, 3489311, 3489312, 3489314, 3489316, 3489317, 3489318, 3489319,
    3489324, 3489331, 3489338, 3489342, 3489344, 3489345, 3489347, 3489353,
    3489356, 3489357, 3489358, 3489366, 3489367, 3489368, 3489369, 3489370,
    3489371, 3489372, 3489378, 3489381, 3489394, 3489395, 3489396, 3489398,
    3489401, 3489409, 3489413, 3489416, 3489420, 3489421, 3489423, 3489424,
    3489425, 3489426, 3489430, 3489432, 3489434, 3489436, 3489438, 3489442,
    3489443, 3489444, 3489445, 3489446, 3489447, 3489449, 3489452, 3489455,
    3489456, 3489459, 3489460, 3489464, 3489469, 3489470, 3489472, 3489476,
    3489479, 3489480, 3489486, 3489488, 3489489, 3489491, 3489492, 3489493,
    3489494, 3489496, 3489498, 3489500, 3489502, 3489503, 3489505, 3489508,
    3489511, 3489512, 3489520, 3489521, 3489522, 3489524, 3489525, 3489527,
    3489528, 3489530, 3489532, 3489533, 3489534, 3489538, 3489543, 3489546,
    3489547, 3489552, 3489556, 3489557, 3489559, 3489560, 3489566, 3489567,
    3489568, 3489569, 3489571, 3489575, 3489576, 3489581, 3489582, 3489587,
    3489588, 3489593, 3489594, 3489603, 3489604, 3489605, 3489607, 3489609,
    3489614, 3489617, 3489618, 3489619, 3489628, 3489630, 3489633, 3489635,
    3489638, 3489639, 3489642, 3489643, 3489644, 3489645, 3489646, 3489647,
    3489653, 3489655, 3489656, 3489657, 3489659, 3489660, 3489664, 3489668,
    3489669, 3489670, 3489672, 3489673, 3489677, 3489680, 3489681, 3489683,
    3489688, 3489689, 3489690, 3489694, 3489696, 3489698, 3489699, 3489704,
    3489706, 3489707, 3489709, 3489710, 3489714, 3489717, 3489721, 3489723,
    3489725, 3489728, 3489729, 3489732, 3489734, 3489736, 3489740, 3489743,
    3489744, 3489745, 3489747, 3489748, 3489750, 3489752, 3489753, 3489755,
    3489757, 3489761, 3489762, 3489763, 3489764, 3489766, 3489767, 3489768,
    3489770, 3489772, 3489773, 3489775, 3489776, 3489777, 3489778, 3489779,
    3489780, 3489785, 3489793, 3489794, 3489796, 3489800, 3489801, 3489803,
    3489804, 3489806, 3489807, 3489809, 3489811, 3489813, 3489814, 3489816,
    3489817, 3489820, 3489821, 3489824, 3489826, 3489829, 3489830, 3489832,
    3489833, 3489836, 3489837, 3489839, 3489842, 3489843, 3489844, 3489849,
    3489851, 3489854, 3489860, 3489863, 3489865, 3489866, 3489871, 3489876,
    3489877, 3489879, 3489883, 3489888, 3489892, 3489893, 3489896, 3489900,
    3489901, 3489903, 3489904, 3489905, 3489908, 3489910, 3489913, 3489923,
    3489924, 3489926, 3489930, 3489933, 3489938, 3489939, 3489940, 3489941,
    3489942, 3489951, 3489956, 3489958, 3489964, 3489967, 3489971, 3489972,
    3489974, 3489977, 3489978, 3489980, 3489984, 3489987, 3489988, 3489989,
    3489990, 3489991, 3489992, 3490001, 3490005, 3490006, 3490008, 3490010,
    3490011, 3490012, 3490014, 3490015, 3490016, 3490018, 3490021, 3490027,
    3490032, 3490036, 3490038, 3490039, 3490042, 3490043, 3490045, 3490046,
    3490048, 3490049, 3490050, 3490053, 3490055, 3490056, 3490058, 3490059,
    3490062, 3490063, 3490065, 3490066, 3490067, 3490068, 3490069, 3490070,
    3490072, 3490077, 3490078, 3490079, 3490082, 3490086, 3490090, 3490092,
    3490093, 3490094, 3490096, 3490101, 3490105, 3490106, 3490114, 3490115,
    3490118, 3490119, 3490120, 3490127, 3490128, 3490132, 3490133, 3490134,
    3490140, 3490148, 3490150, 3490151, 3490154, 3490157, 3490163, 3490167,
    3490171, 3490172, 3490176, 3490180, 3490182, 3490186, 3490189, 3490190,
    3490193, 3490195, 3490196, 3490197, 3490200, 3490201, 3490205, 3490206,
    3490208, 3490215, 3490217, 3490219, 3490223, 3490224, 3490225, 3490228,
    3490230, 3490232, 3490235, 3490236, 3490237, 3490240, 3490242, 3490245,
    3490246, 3490247, 3490248, 3490249, 3490250, 3490257, 3490259, 3490260,
    3490262, 3490267, 3490270, 3490271, 3490272, 3490273, 3490275, 3490278,
    3490279, 3490280, 3490285, 3490287, 3490288, 3490292, 3490297, 3490301,
    3490303, 3490305, 3490306, 3490308, 3490309, 3490310, 3490313, 3490315,
    3490316, 3490321, 3490322, 3490323, 3490325, 3490328, 3490331, 3490333,
    3490338, 3490341, 3490343, 3490344, 3490345, 3490347, 3490348, 3490350,
    3490351, 3490359, 3490361, 3490363, 3490365, 3490367, 3490370, 3490371,
    3490374, 3490380, 3490384, 3490389, 3490391, 3490393, 3490395, 3490401,
    3490403, 3490405, 3490406, 3490407, 3490408, 3490409, 3490412, 3490417,
    3490419, 3490420, 3490421, 3490423, 3490424, 3490427, 3490430, 3490432,
    3490434, 3490435, 3490436, 3490440, 3490452, 3490453, 3490454, 3490463,
    3490464, 3490466, 3490468, 3490470, 3490473, 3490474, 3490480, 3490481,
    3490482, 3490483, 3490485, 3490487, 3490492, 3490496, 3490498, 3490499,
    3490500, 3490501, 3490511, 3490512, 3490517, 3490518, 3490519, 3490522,
    3490523, 3490524, 3490525, 3490527, 3490530, 3490532, 3490537, 3490538,
    3490539, 3490542, 3490546, 3490551, 3490553, 3490554, 3490555, 3490559,
    3490560, 3490561, 3490565, 3490567, 3490570, 3490575, 3490578, 3490580,
    3490581, 3490582, 3490586, 3490589, 3490590, 3490592, 3490596, 3490597,
    3490598, 3490603, 3490604, 3490613, 3490619, 3490625, 3490628, 3490629,
    3490630, 3490631, 3490635, 3490640, 3490642, 3490644, 3490653, 3490658,
    3490662, 3490666, 3490668, 3490672, 3490675, 3490676, 3490677, 3490681,
    3490691, 3490701, 3490705, 3490706, 3490707, 3490710, 3490716, 3490718,
    3490722, 3490723, 3490724, 3490725, 3490726, 3490728, 3490731, 3490734,
    3490742, 3490743, 3490744, 3490748, 3490749, 3490755, 3490760, 3490768,
    3490776, 3490781, 3490782, 3490786, 3490787, 3490788, 3490789, 3490797,
    3490802, 3490803, 3490807, 3490815, 3490816, 3490828, 3490832, 3490841,
    3490849, 3490852, 3490854, 3490855, 3490857, 3490859, 3490860, 3490862,
    3490869, 3490872, 3490874, 3490876, 3490878, 3490880, 3490881, 3490887,
    3490895, 3490906, 3490907, 3490911, 3490915, 3490917, 3490921, 3490925,
    3490927, 3490928, 3490929, 3490931, 3490933, 3490940, 3490941, 3490944,
    3490945, 3490947, 3490949, 3490955, 3490957, 3490963, 3490973, 3490974,
    3490977, 3490978, 3490979, 3490980, 3490982, 3490983, 3490991, 3490992,
    3490995, 3490999, 3491002, 3491005, 3491034, 3491043, 3491048, 3491053,
    3491071, 3491072, 3491422, 3491864, 3491865, 3491866, 3491867, 3491868,
    3491869, 3491870, 3491871, 3491872, 3491873, 3491874, 3491875, 3491876,
    3491877, 3491878, 3491879, 3491880, 3491881, 3491882, 3491884, 3491885,
    3491886, 3491887, 3491888, 3491890, 3491891, 3491892, 3491894, 3491896,
    3491897, 3491898, 3491899, 3491900, 3491901, 3491903, 3491904, 3491906,
    3491907, 3491908, 3491909, 3491910, 3491911, 3491912, 3491913, 3491914,
    3491916, 3491917, 3491919, 3491920, 3491921, 3491922, 3491923, 3491924,
    3491926, 3491927, 3491930, 3491931, 3491932, 3491934, 3491940, 3491945,
    3491946, 3491947, 3491951, 3491964, 3491965, 3491967, 3491969, 3491970,
    3491973, 3491975, 3491976, 3491978, 3491979, 3491982, 3491983, 3491993,
    3491998, 3492040, 3492041, 3492922, 3492940, 3493189, 3493190, 3493194,
    3493195, 3493196, 3493198, 3493200, 3493202, 3493203, 3493205, 3493206,
    3493207, 3493211, 3493214, 3493216, 3493263, 3493264, 3493265, 3493266,
    3493268, 3493269, 3493272, 3493273, 3493283, 3493353, 3493354, 3493355,
    3493356, 3493357, 3493358, 3493360, 3493361, 3493363, 3493450, 3493451,
    3493458, 3493459, 3493477, 3536220, 3536226, 3536272, 3536273, 3536274,
    3536275, 3536276, 3536304, 3536305, 3536306, 3536307, 3536308, 3536309,
    3536310, 3536333, 3539870, 3539872, 3539873, 3539874, 3539875, 3539876,
    3539877, 3539878, 3539879, 3539880, 3539881, 3539882, 3539883, 3539884,
    3539885, 3539886, 3539887, 3539888, 3539889, 3539890, 3539891, 3539892,
    3539893, 3539894, 3539895, 3539896, 3539897, 3539898, 3539899, 3539900,
    3539901, 3539902, 3539903, 3539905, 3539907, 3539908, 3539909, 3539912,
    3539913, 3539914, 3539915, 3539916, 3539917, 3539918, 3539919, 3539920,
    3539921, 3539922, 3539923, 3539924, 3539925, 3539926, 3539927, 3539928,
    3539930, 3540029, 3540054};

struct dataarray {
  dataarray() : name(), data() {}
  string name;
  vector<uint32_t> data;
};

void testIssue18(vector<shared_ptr<IntegerCODEC>> codecs) {
  std::cout << "[testing issue 18]" << std::endl;
  const int N = sizeof(somearray) / sizeof(uint32_t);
  vector<uint32_t> data(somearray, somearray + N);
  for (shared_ptr<IntegerCODEC> codec : codecs) {
    std::cout << "testing issue 18 ... with codec " << codec->name();
    std::cout.flush();

    vector<uint32_t> dirtycopy(somearray, somearray + N);
    vector<uint32_t> compressedbuffer(N + 1024);
    vector<uint32_t> recoverybuffer(N + 1024);
    size_t nvalue = compressedbuffer.size();
    codec->encodeArray(dirtycopy.data(), dirtycopy.size(),
                       compressedbuffer.data(), nvalue);
    size_t recoveredvalues = recoverybuffer.size();
    codec->decodeArray(compressedbuffer.data(), nvalue, recoverybuffer.data(),
                       recoveredvalues);
    recoverybuffer.resize(recoveredvalues);
    if (data != recoverybuffer) {
      cout << "Problem with  " << codec->name() << endl;
      for (size_t i = 0; i < data.size(); ++i)
        cout << i << " " << data[i] << " " << recoverybuffer[i] << endl;
      throw std::logic_error("bug");
    } else {
      std::cout << "... ok!" << std::endl;
    }
  }
}

void testIssue24(vector<shared_ptr<IntegerCODEC>> codecs, vector<std::string> codecs_name) {
  std::cout << "[testing issue 24]" << std::endl;
  size_t N = 10 * 1000;
  vector<uint32_t> mydata_pristine(N);
  for (uint32_t i = 0; i < N; ++i)
    mydata_pristine[i] = 3 * i;
  for(size_t i = 0; i < codecs_name.size(); i++) {
    shared_ptr<IntegerCODEC> codec = codecs[i];
    std::cout << "testing issue 24 ... with codec " << codec->name();
    std::cout << " (" << codecs_name[i] << ") ";
    
    std::cout.flush();
    vector<uint32_t> mydata(mydata_pristine);
    ///////////
    //
    // You need some "output" container. You are responsible
    // for allocating enough memory.
    //
    vector<uint32_t> compressed_output(N + 1024);
    // N+1024 should be plenty
    //
    //
    size_t compressedsize = compressed_output.size();
    codec->encodeArray(mydata.data(), mydata.size(), compressed_output.data(),
                      compressedsize);
    if(mydata != mydata_pristine) {
      std::cout << " [input modified]" << std::endl;
    }
    //
    // if desired, shrink back the array:
    compressed_output.resize(compressedsize);
    compressed_output.shrink_to_fit();
    // display compression rate:
    cout << setprecision(3);
    cout << "You are using "
         << 32.0 * static_cast<double>(compressed_output.size()) /
                static_cast<double>(mydata.size())
         << " bits per integer. " << endl;
    //
    // You are done!... with the compression...
    //
    ///
    // decompressing is also easy:
    //
    vector<uint32_t> mydataback(N);
    size_t recoveredsize = mydataback.size();
    //
    codec->decodeArray(compressed_output.data(), compressed_output.size(),
                      mydataback.data(), recoveredsize);
    mydataback.resize(recoveredsize);
    //
    // That's it for compression!
    //
    if(mydataback.size() != mydata.size()) {
    throw runtime_error("lengths differ ");
    }
    if (mydataback != mydata_pristine) {
      for(size_t ii = 0; ii < mydata.size(); ii ++) {
        std::cout << mydata_pristine[ii] << " " << mydataback[ii];
        if( mydata_pristine[ii] != mydataback[ii]) {
          std::cout << "**********";
        }
        std::cout << std::endl;
      }
      std::cout << "bug!" << std::endl;
      
      throw runtime_error("bug!");
    } else {
      std::cout << "ok!" << std::endl;
    }
  }
}

void testSmall(vector<shared_ptr<IntegerCODEC>> codecs) {
  std::cout << "[testing small]" << std::endl;
  vector<uint32_t> data;
  data.push_back(1U);
  data.push_back(3U);
  data.push_back(5U);
  data.push_back(15U + 1024U);
  data.push_back(21U + 1024U);

  for (shared_ptr<IntegerCODEC> codec : codecs) {
    std::cout << "testing small ... with codec " << codec->name();
    vector<uint32_t> dirtycopy(data);
    vector<uint32_t> compressedbuffer(data.size() + 1024);
    vector<uint32_t> recoverybuffer(data.size() + 1024);
    size_t nvalue = compressedbuffer.size();
    codec->encodeArray(dirtycopy.data(), dirtycopy.size(),
                       compressedbuffer.data(), nvalue);
    size_t recoveredvalues = recoverybuffer.size();
    codec->decodeArray(compressedbuffer.data(), nvalue, recoverybuffer.data(),
                       recoveredvalues);
    recoverybuffer.resize(recoveredvalues);
    if (data != recoverybuffer) {
      cout << "Problem with  " << codec->name() << endl;
      for (size_t i = 0; i < data.size(); ++i)
        cout << i << " " << data[i] << " " << recoverybuffer[i] << endl;
      throw std::logic_error("bug");
    } else {
      std::cout << "... ok!" << std::endl;
    }
  }
}

void sillyunittest(vector<dataarray> datas, vector<uint32_t> &compressedbuffer,
                   vector<uint32_t> &recoverybuffer, IntegerCODEC &codec) {
  for (vector<dataarray>::const_iterator i = datas.begin(); i != datas.end();
       ++i) {
    const vector<uint32_t> &data = i->data;
    vector<uint32_t> dirtycopy(data);
    size_t nvalue = compressedbuffer.size();
    codec.encodeArray(dirtycopy.data(), dirtycopy.size(),
                      compressedbuffer.data(), nvalue);
    size_t recoveredvalues = recoverybuffer.size();
    codec.decodeArray(compressedbuffer.data(), nvalue, recoverybuffer.data(),
                      recoveredvalues);
    recoverybuffer.resize(recoveredvalues);
    if (data != recoverybuffer) {
      cout << "Problem with  " << codec.name() << endl;
      size_t howmany = 0;
      for (size_t ii = 0; ii < data.size(); ++ii) {
        if (data[ii] != recoverybuffer[ii]) {
          if (++howmany > 5) {
            cout << "..." << endl;
            break;
          }
          cout << ii << " " << data[ii] << " " << recoverybuffer[ii] << endl;
        }
      }
      throw std::logic_error("bug");
    }
  }
  cout.flush();
}

void unittrivial(bool deltacode, vector<shared_ptr<IntegerCODEC>> &allcodecs,
                 const uint32_t S) {
  const uint32_t N = 1U << S;
  vector<dataarray> datas;
  dataarray X;
  vector<uint32_t> d(N);
  for (uint32_t k = 0; k < N; ++k)
    d[k] = k;
  X.data = d;
  if (deltacode)
    delta(0u, X.data.data(), X.data.size());

  ostringstream convert;
  convert << N;
  X.name = convert.str();
  datas.push_back(X);
  vector<uint32_t> compressedbuffer;
  compressedbuffer.resize(N * 2);
  vector<uint32_t> recoverybuffer;
  recoverybuffer.resize(N);
  for (auto i : allcodecs)
    sillyunittest(datas, compressedbuffer, recoverybuffer, *i);
}

void unit(bool deltacode, vector<shared_ptr<IntegerCODEC>> &allcodecs,
          const uint32_t S, int seed) {
  const uint32_t N = 1U << S;
  ClusteredDataGenerator cdg(seed);

  vector<dataarray> datas;
  uint32_t NUMBER = 1; // Increase as needed
  for (uint32_t gap = 1; gap + S <= 31; gap += 1) {
    for (uint32_t T = 0; T < NUMBER; ++T) {
      dataarray X;
      X.data = cdg.generateClustered(N, 1U << (gap + S));

      if (deltacode)
        delta(0u, X.data.data(), X.data.size());

      ostringstream convert;
      convert << gap;
      X.name = convert.str();
      datas.push_back(X);
    }
  }
  vector<uint32_t> compressedbuffer;
  compressedbuffer.resize(N * 2);
  vector<uint32_t> recoverybuffer;
  recoverybuffer.resize(N);
  for (auto i : allcodecs)
    sillyunittest(datas, compressedbuffer, recoverybuffer, *i);
}

int test1(intersectionfunction f, bool testwriteback) {

  const uint32_t firstpost[13] = {
      27181,  35350,  39241,  39277,  39278,  44682,  64706,
      120447, 120450, 159274, 159290, 173895, 173942,
  };
  const uint32_t secondpost[13] = {25369, 28789, 28790, 28792, 28794,
                                   28797, 37750, 42317, 68797, 68877,
                                   68881, 68990, 85488};
  vector<uint32_t> inter(13);
  size_t s = f(firstpost, 13, secondpost, 13, inter.data());
  inter.resize(s);
  vector<uint32_t> correct(13);
  size_t cs =
      classicalintersection(firstpost, 13, secondpost, 13, correct.data());
  correct.resize(cs);
  if (inter != correct) {
    cout << inter.size() << " " << correct.size() << endl;
    for (size_t i = 0; (i < inter.size()) && (i < correct.size()); ++i)
      cout << i << " " << inter[i] << " " << correct[i] << endl;
    return 1;
  }
  if (!testwriteback)
    return 0;
  vector<uint32_t> inter2(firstpost, firstpost + 13);
  size_t s2 = f(inter2.data(), 13, secondpost, 13, inter2.data());
  inter2.resize(s2);
  if (inter2 != correct)
    return 2;
  return 0;
}

int test2(intersectionfunction f) {
  const uint32_t firstpost[5] = {12635, 12921, 12923, 12924, 12926};

  const uint32_t secondpost[173] = {
      3756,  11996, 12044, 12049, 12109, 12128, 12131, 12141, 12142, 12150,
      12154, 12160, 12167, 12168, 12172, 12177, 12201, 12208, 12215, 12216,
      12223, 12228, 12232, 12233, 12234, 12235, 12236, 12240, 12241, 12242,
      12243, 12254, 12255, 12256, 12257, 12259, 12260, 12261, 12262, 12264,
      12265, 12266, 12275, 12295, 12471, 12482, 12486, 12508, 12509, 12510,
      12511, 12512, 12530, 12536, 12572, 12573, 12589, 12607, 12609, 12611,
      12630, 12631, 12632, 12633, 12634, 12635, 12636, 12653, 12655, 12657,
      12668, 12672, 12685, 12702, 12716, 12721, 12741, 12745, 12750, 12755,
      12757, 12761, 12765, 12767, 12768, 12794, 12802, 12803, 12823, 12842,
      12851, 12871, 12891, 12893, 12894, 12895, 12896, 12897, 12915, 12917,
      12918, 12919, 12920, 12921, 12922, 12923, 12924, 12925, 12927, 12929,
      12932, 12933, 12934, 12935, 12936, 12937, 12938, 12939, 12942, 12946,
      12951, 12955, 12963, 12972, 13011, 13013, 13014, 13015, 13017, 13032,
      13033, 13036, 13042, 13050, 13051, 13052, 13057, 13058, 13060, 13090,
      13120, 13132, 13136, 13147, 13185, 13191, 13192, 13193, 13194, 13195,
      13198, 13202, 13205, 13219, 13228, 13230, 13232, 13233, 13238, 13240,
      13246, 13248, 13277, 13278, 13281, 13282, 13283, 13284, 13291, 13320,
      13338, 13346, 13347};
  vector<uint32_t> inter(173);
  size_t s = f(firstpost, 5, secondpost, 173, inter.data());
  inter.resize(s);
  vector<uint32_t> correct(173);
  size_t cs =
      classicalintersection(firstpost, 5, secondpost, 173, correct.data());
  correct.resize(cs);
  if (inter != correct) {
    cout << inter.size() << " " << correct.size() << endl;
    cout << " correct answer:" << endl;
    for (size_t i = 0; i < correct.size(); ++i)
      cout << i << " " << correct[i] << endl;
    cout << " bad answer:" << endl;
    for (size_t i = 0; i < inter.size(); ++i)
      cout << i << " " << inter[i] << endl;
    return 1;
  }
  return 0;
}

int test3(intersectionfunction f) {

  vector<uint32_t> firstpost;
  vector<uint32_t> secondpost;
  vector<uint32_t> trueinter;

  for (uint32_t i = 10; i < 31; ++i) {
    firstpost.push_back((1U << i) | 3U);
    trueinter.push_back((1U << i) | 3U);
    for (uint32_t j = 3; j < 1000; j += 11) {
      secondpost.push_back((1U << i) | j);
    }
    firstpost.push_back((1U << i) | 1001U);
  }
  vector<uint32_t> inter(firstpost.size());
  size_t s = f(firstpost.data(), firstpost.size(), secondpost.data(),
               secondpost.size(), inter.data());
  inter.resize(s);
  if (inter != trueinter) {
    cout << inter.size() << " " << trueinter.size() << endl;
    for (size_t i = 0; (i < inter.size()) && (i < trueinter.size()); ++i)
      cout << i << " " << inter[i] << " " << trueinter[i] << endl;
    return 1;

    return 1;
  }
  return 0;
}

void tellmeaboutmachine() {
  cout << "number of bytes in ostream::pos_type = " << sizeof(ostream::pos_type)
       << endl;
  cout << "number of bytes in size_t = " << sizeof(size_t) << endl;
  cout << "number of bytes in int = " << sizeof(int) << endl;
  cout << "number of bytes in long = " << sizeof(long) << endl;
#if __LITTLE_ENDIAN__
  cout << "you have little endian machine" << endl;
#endif
#if __BIG_ENDIAN__
  cout << "you have a big endian machine" << endl;
#endif
#if __CHAR_BIT__
  if (__CHAR_BIT__ != 8)
    cout << "on your machine, chars don't have 8bits???" << endl;
#endif
#if __GNUG__
  cout << "GNU GCC compiler detected." << endl;
#else
  cout << "Non-GCC compiler." << endl;
#endif
}

template <typename T> void testFindSimple() {
  T codec;
  const int max = 256;
  uint32_t ints[max];
  for (int i = 0; i < max; i++)
    ints[i] = i;

  // encode in a buffer
  vector<uint32_t> compressedbuffer(max * sizeof(uint32_t) + 1024);
  size_t nvalue = compressedbuffer.size();
  codec.encodeArray(ints, max, compressedbuffer.data(), nvalue);

  uint32_t k = 0;
  for (int i = 0; i < max; i++) {
    size_t pos = codec.findLowerBound(compressedbuffer.data(), max, i, &k);
    if (k != (uint32_t)i && pos != static_cast<size_t>(i)) {
      cout << codec.name() << "::findLowerBoundDelta failed with " << i << endl;
      throw std::logic_error("bug");
    }
  }

  cout << codec.name() << "::findLowerBoundDelta ok" << endl;
}

template <typename T> void testFindAdvanced() {
  T codec;
  const int max = 2944;
  uint32_t ints[max];
  // print random seed to make the test reproducable
  time_t t = ::time(0);
  cout << "Seed is " << t << endl;
  ::srand(static_cast<unsigned int>(t));

  // initialize
  for (int i = 0; i < max; i++)
    ints[i] = 1 + i * 2;

  // encode in a buffer
  vector<uint32_t> compressedbuffer(max * sizeof(uint32_t) + 1024);
  size_t nvalue = compressedbuffer.size();
  codec.encodeArray(ints, max, compressedbuffer.data(), nvalue);

  uint32_t k1 = 0;
  uint32_t k2 = 0;
  for (uint32_t i = 0; i < max * 2; i++) {
    size_t pos1 = codec.findLowerBound(compressedbuffer.data(), max, i, &k1);
    uint32_t *it = std::lower_bound(&ints[0], &ints[max], i);
    size_t pos2 = static_cast<size_t>(it - &ints[0]);
    k2 = *it;
    // key not found?
    if (it == &ints[max] && pos1 != max) {
      cout << codec.name() << "::findLowerBoundDelta failed at line "
           << __LINE__ << " (i = " << i << ")" << endl;
      throw std::logic_error("bug");
    }
    // otherwise make sure both results are equal
    if (k1 != k2) {
      cout << codec.name() << "::findLowerBoundDelta failed at line "
           << __LINE__ << " (i = " << i << ")" << endl;
      throw std::logic_error("bug");
    }
    if (pos1 != pos2) {
      cout << codec.name() << "::findLowerBoundDelta failed at line "
           << __LINE__ << " (i = " << i << ")" << endl;
      throw std::logic_error("bug");
    }
  }

  cout << codec.name() << "::findLowerBoundDelta ok" << endl;
}

template <typename T> void testInsert() {
  T codec;
  const int max = 256;
  srand(0);
  uint32_t ints[max];
  for (int i = 0; i < max; i++)
    ints[i] = rand();
  // encode in a buffer
  vector<uint32_t> compressedbuffer(max * sizeof(uint32_t) + 1024);
  vector<uint32_t> decomp(max);
  vector<uint32_t> sofar;

  size_t currentsize = 0;
  for (int i = 0; i < max; i++) {
    currentsize = codec.insert(compressedbuffer.data(),
                               static_cast<uint32_t>(currentsize), ints[i]);
    size_t howmany = decomp.size();
    codec.decodeArray(compressedbuffer.data(), currentsize, decomp.data(),
                      howmany);
    sofar.push_back(ints[i]);
    std::sort(sofar.begin(), sofar.end());
    if (howmany != sofar.size())
      cout << howmany << " " << sofar.size() << " " << i << endl;
    assert(howmany == sofar.size());
    for (size_t j = 0; j < howmany; ++j) {
      if (decomp[j] != sofar[j]) {
        cout << codec.name() << "::insert failed with i = " << i
             << ", j = " << j << endl;
        for (size_t jj = 0; jj < howmany; ++jj) {
          cout << jj << " -->  stored: " << decomp[jj] << " correct: " << sofar[jj]
               << endl;
        }
        for (size_t jj = 0; jj < currentsize * 4; ++jj) {
          cout << (uint32_t)(((uint8_t *)compressedbuffer.data())[jj]) << " ";
        }
        cout << endl;

        throw std::logic_error("bug");
      }
    }
  }

  cout << codec.name() << "::insert ok" << endl;
}

// same as testAppend<>, but uses encodeByteArray/decodeByteArray
// to avoid padding
template <typename T> void testAppendByteArray() {
  T codec;
  const int max = 256;
  srand(0);
  uint32_t ints[max];
  for (int i = 0; i < max; i++)
    ints[i] = rand();
  std::sort(&ints[0], &ints[max]);

  // encode in a buffer
  vector<uint8_t> compressedbuffer(max * sizeof(uint32_t) + 1024);
  vector<uint32_t> decomp(max);
  vector<uint32_t> sofar;

  size_t currentsize = 0;
  for (int i = 0; i < max; i++) {
    currentsize = codec.appendToByteArray(compressedbuffer.data(), currentsize,
                                          i > 0 ? ints[i - 1] : 0, ints[i]);
    size_t howmany = decomp.size() * 4;
    // we pad if needed
    if (codec.name() == "VByteDelta") { // must pad with 1s
      for (unsigned int k = 0; k < (currentsize + 3) / 4 * 4 - currentsize;
           ++k) {
        compressedbuffer[currentsize + k] = 0xFF;
      }
    } else { // regular padding with 0s
      for (unsigned int k = 0; k < (currentsize + 3) / 4 * 4 - currentsize;
           ++k) {
        compressedbuffer[currentsize + k] = 0;
      }
    }
    codec.decodeArray((const uint32_t *)compressedbuffer.data(),
                      (currentsize + 3) / 4, decomp.data(), howmany);
    // codec.decodeFromByteArray(compressedbuffer.data(), currentsize,
    //            decomp.data(), howmany);
    sofar.push_back(ints[i]);
    if (howmany != sofar.size())
      cout << howmany << " " << sofar.size() << " " << i << endl;
    assert(howmany == sofar.size());

    for (size_t j = 0; j < howmany; ++j) {
      if (decomp[j] != sofar[j]) {
        cout << codec.name() << "::append failed with i = " << i
             << ", j = " << j << endl;
        for (size_t jj = 0; jj < howmany; ++jj) {
          cout << jj << " -->  stored: " << decomp[jj] << " correct: " << sofar[jj]
               << endl;
        }
        cout << endl;

        throw std::logic_error("bug");
      }
    }
  }

  cout << codec.name() << "::append ok" << endl;
}

template <typename T> void testAppend() {
  T codec;
  const int max = 256;
  srand(0);
  uint32_t ints[max];
  for (int i = 0; i < max; i++)
    ints[i] = rand();
  std::sort(&ints[0], &ints[max]);

  // encode in a buffer
  vector<uint32_t> compressedbuffer(max * sizeof(uint32_t) + 1024);
  vector<uint32_t> decomp(max);
  vector<uint32_t> sofar;

  size_t currentsize = 0;
  for (int i = 0; i < max; i++) {
    currentsize =
        codec.append((uint8_t *)compressedbuffer.data(), currentsize, ints[i]);
    size_t howmany = decomp.size() * 4;
    codec.decodeArray(compressedbuffer.data(), currentsize, decomp.data(),
                      howmany);
    sofar.push_back(ints[i]);
    if (howmany != sofar.size())
      cout << howmany << " " << sofar.size() << " " << i << endl;
    assert(howmany == sofar.size());

    for (size_t jj = 0; jj < howmany; ++jj) {
      if (decomp[jj] != sofar[jj]) {
        cout << codec.name() << "::append failed with i = " << i
             << ", j = " << jj << endl;
        for (size_t jjj = 0; jjj < howmany; ++jjj) {
          cout << jjj << " -->  stored: " << decomp[jjj] << " correct: " << sofar[jjj]
               << endl;
        }
        cout << endl;

        throw std::logic_error("bug");
      }
    }
  }

  cout << codec.name() << "::append ok" << endl;
}

template <typename T> void testSelectSimple() {
  T codec;
  const int max = 256;
  uint32_t ints[max];
  for (int i = 0; i < max; i++)
    ints[i] = i;
  // encode in a buffer
  vector<uint32_t> compressedbuffer(max * sizeof(uint32_t) + 1024);
  size_t nvalue = compressedbuffer.size();
  codec.encodeArray(ints, max, compressedbuffer.data(), nvalue);

  compressedbuffer.resize(nvalue);
  compressedbuffer.shrink_to_fit();
  for (int i = 0; i < max; i++) {

    uint32_t k = codec.select(compressedbuffer.data(), i);
    if (k != (uint32_t)i) {
      cout << codec.name() << "::select failed with " << i << endl;
      throw std::logic_error("bug");
    }
  }

  cout << codec.name() << "::select ok" << endl;
}

template <typename T> void testSelectSimpleOdd() {
  T codec;
  const int max = 259;
  uint32_t ints[max];
  for (int i = 0; i < max; i++)
    ints[i] = i;

  // encode in a buffer
  vector<uint32_t> compressedbuffer(max * sizeof(uint32_t) + 1024);
  size_t nvalue = compressedbuffer.size();
  codec.encodeArray(ints, max, compressedbuffer.data(), nvalue);
  compressedbuffer.resize(nvalue);
  compressedbuffer.shrink_to_fit();
  for (int i = 0; i < max; i++) {
    uint32_t k = codec.select(compressedbuffer.data(), i);
    if (k != (uint32_t)i) {
      cout << codec.name() << "::odd select failed with " << i << " got back "
           << k << endl;
      throw std::logic_error("bug");
    }
  }

  cout << codec.name() << "::select ok" << endl;
}

template <typename T> void testSelectAdvanced() {
  T codec;
  const int max = 2944;
  uint32_t ints[max];
  // print random seed to make the test reproducable
  time_t t = ::time(0);
  cout << "Seed is " << t << endl;
  ::srand(static_cast<unsigned int>(t));

  // fill array with "random" values
  for (int i = 0; i < max; i++)
    ints[i] = 1 + i * 2;
  std::random_shuffle(&ints[0], &ints[max]);

  // encode in a buffer
  vector<uint32_t> compressedbuffer(max * sizeof(uint32_t) + 1024);
  size_t nvalue = compressedbuffer.size();
  codec.encodeArray(ints, max, compressedbuffer.data(), nvalue);

  uint32_t k1, k2;
  for (int i = 0; i < max; i++) {
    k1 = codec.select(compressedbuffer.data(), i);
    k2 = (uint32_t)ints[i];

    if (k1 != k2) {
      cout << "max = " << max << endl;
      cout << "got back " << k1 << endl;

      for (int j = 0; j <= i; j++) { // shit
        cout << j << "-->" << ints[j] << " " << endl;
      }

      cout << codec.name() << "::selectDelta failed at line " << __LINE__
           << " (i = " << i << ")" << endl;
      throw std::logic_error("bug");
    }
  }

  cout << codec.name() << "::selectDelta ok" << endl;
}

int main() {
  testInsert<StreamVByteD1>();
  testInsert<VarIntGB<true>>();
  testInsert<VariableByte<true>>();
  testInsert<VByte<true>>();

  testAppendByteArray<StreamVByteD1>();
  testAppendByteArray<VarIntGB<true>>();
  testAppendByteArray<VByte<true>>();
  testAppendByteArray<MaskedVByte<true>>();
  testAppendByteArray<VariableByte<true>>();
  testAppendByteArray<ForCODEC>();
  testAppendByteArray<FrameOfReference>();
  testAppendByteArray<SIMDFrameOfReference>();

  testAppend<ForCODEC>();
  testAppend<FrameOfReference>();
  testAppend<SIMDFrameOfReference>();

  testSelectSimple<ForCODEC>();
  testSelectSimple<SIMDFrameOfReference>();
  testSelectSimple<FrameOfReference>();
  testSelectSimple<VarIntGB<true>>();
  testSelectSimple<MaskedVByte<true>>();
  testSelectSimple<VariableByte<true>>();
  testSelectSimple<VByte<true>>();
  testSelectSimple<
      SIMDBinaryPacking<SIMDIntegratedBlockPacker<RegularDeltaSIMD, true>>>();
  testSelectSimple<StreamVByteD1>();

  testSelectSimpleOdd<SIMDFrameOfReference>();
  testSelectSimpleOdd<FrameOfReference>();
  testSelectSimpleOdd<ForCODEC>();
  testSelectSimpleOdd<VarIntGB<true>>();
  testSelectSimpleOdd<MaskedVByte<true>>();
  testSelectSimpleOdd<VariableByte<true>>();
  testSelectSimpleOdd<VByte<true>>();
  testSelectSimpleOdd<StreamVByteD1>();

  testSelectAdvanced<SIMDFrameOfReference>();
  testSelectAdvanced<FrameOfReference>();
  testSelectAdvanced<ForCODEC>();
  testSelectAdvanced<VarIntGB<true>>();
  testSelectAdvanced<MaskedVByte<true>>();
  testSelectAdvanced<VariableByte<true>>();
  testSelectAdvanced<VByte<true>>();
  testSelectAdvanced<
      SIMDBinaryPacking<SIMDIntegratedBlockPacker<RegularDeltaSIMD, true>>>();
  testSelectAdvanced<StreamVByteD1>();

  testFindSimple<SIMDFrameOfReference>();
  testFindSimple<FrameOfReference>();
  testFindSimple<ForCODEC>();
  testFindSimple<VarIntGB<true>>();
  testFindSimple<MaskedVByte<true>>();
  testFindSimple<VariableByte<true>>();
  testFindSimple<VByte<true>>();
  testFindSimple<
      SIMDBinaryPacking<SIMDIntegratedBlockPacker<RegularDeltaSIMD, true>>>();
  testFindSimple<StreamVByteD1>();

  testFindAdvanced<SIMDFrameOfReference>();
  testFindAdvanced<FrameOfReference>();
  testFindAdvanced<ForCODEC>();
  testFindAdvanced<VarIntGB<true>>();
  testFindAdvanced<MaskedVByte<true>>();
  testFindAdvanced<VariableByte<true>>();
  testFindAdvanced<VByte<true>>();
  testFindAdvanced<
      SIMDBinaryPacking<SIMDIntegratedBlockPacker<RegularDeltaSIMD, true>>>();
  testFindAdvanced<StreamVByteD1>();

  for (string n : IntersectionFactory::allNames()) {
    int error = 0;
    intersectionfunction interfnc = IntersectionFactory::getFromName(n);
    cout << "testing " << n << " ... ";
    cout.flush();
    int code;
    if ((code = test1(interfnc, false)) == 0)
      cout << "ok ";
    else {
      cout << " Error" << code << " ";
      ++error;
    }
    if ((code = test2(interfnc)) == 0)
      cout << "ok ";
    else {
      cout << " Error" << code << endl;
      ++error;
    }
    if ((code = test3(interfnc)) == 0)
      cout << "ok" << endl;
    else {
      cout << " Error" << code << endl;
      ++error;
    }
    assert(error == 0);
    (void)error;
  }

  vector<shared_ptr<IntegerCODEC>> allcodecs = CODECFactory::allSchemes();
  testIssue24(allcodecs, CODECFactory::allNames());
  testSmall(allcodecs);
  testIssue18(allcodecs);

  for (int k = 0; k < 10; ++k) {
    cout << k << " ";
    cout.flush();
    unittrivial(true, allcodecs, 10);
    unittrivial(false, allcodecs, 10);
    unit(true, allcodecs, 10, 1374809627);
    unit(false, allcodecs, 10, 1374809627);
  }
  cout << endl;

  for (int k = 0; k < 10; ++k) {
    cout << k << " ";
    cout.flush();
    unit(true, allcodecs, 14, 1374809627);
    unit(false, allcodecs, 14, 1374809627);
  }
  cout << endl;

  for (int k = 0; k < 100; ++k) {
    cout << k << " ";
    cout.flush();
    unit(false, allcodecs, 7, 1374809652 + k);
    unit(true, allcodecs, 7, 1374809652 + k);
  }
  cout << endl;

  tellmeaboutmachine();

  cout << "Code is probably ok." << endl;
}
