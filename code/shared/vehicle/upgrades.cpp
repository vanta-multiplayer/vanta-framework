#include "upgrades.h"

namespace VantaMP::Shared {
    int UpgradeSlot(std::uint16_t us) {
        // Ported from MTA CVehicleUpgrades::GetSlotFromUpgrade. The IDs
        // are GTA's, not MTA's — one slot per part so a second spoiler
        // replaces the first instead of stacking meshes.
        if (us == 1011 || us == 1012 || us == 1111 || us == 1112 || us == 1142 || us == 1143 || us == 1144 || us == 1145) {
            return 0;
        }

        if (us == 1004 || us == 1005 || us == 1142 || us == 1143 || us == 1144 || us == 1145) {
            return 1;
        }

        if (us == 1000 || us == 1001 || us == 1002 || us == 1003 || us == 1014 || us == 1015 || us == 1016 || us == 1023 || us == 1049 || us == 1050 ||
            us == 1058 || us == 1060 || us == 1138 || us == 1139 || us == 1146 || us == 1147 || us == 1158 || us == 1162 || us == 1163 || us == 1164) {
            return 2;
        }

        if (us == 1007 || us == 1017 || us == 1026 || us == 1027 || us == 1030 || us == 1031 || us == 1036 || us == 1039 || us == 1040 || us == 1041 ||
            us == 1042 || us == 1047 || us == 1048 || us == 1051 || us == 1052 || us == 1056 || us == 1057 || us == 1062 || us == 1063 || us == 1069 ||
            us == 1070 || us == 1071 || us == 1072 || us == 1090 || us == 1093 || us == 1094 || us == 1095 || us == 1099 || us == 1101 || us == 1102 ||
            us == 1106 || us == 1107 || us == 1108 || us == 1118 || us == 1119 || us == 1120 || us == 1121 || us == 1122 || us == 1124 || us == 1133 ||
            us == 1134 || us == 1137) {
            return 3;
        }

        if (us == 1115 || us == 1116) {
            return 4;
        }

        if (us == 1109 || us == 1110) {
            return 5;
        }

        if (us == 1013 || us == 1024) {
            return 6;
        }

        if (us == 1006 || us == 1032 || us == 1033 || us == 1035 || us == 1038 || us == 1053 || us == 1054 || us == 1055 || us == 1061 || us == 1067 ||
            us == 1068 || us == 1088 || us == 1091 || us == 1103 || us == 1128 || us == 1130 || us == 1131) {
            return 7;
        }

        if (us == 1008 || us == 1009 || us == 1010) {
            return 8;
        }

        if (us == 1087) {
            return 9;
        }

        if (us == 1086) {
            return 10;
        }

        if (us == 1025 || us == 1073 || us == 1074 || us == 1075 || us == 1076 || us == 1077 || us == 1078 || us == 1079 || us == 1080 || us == 1081 ||
            us == 1082 || us == 1083 || us == 1084 || us == 1085 || us == 1096 || us == 1097 || us == 1098) {
            return 12;
        }

        if (us == 1018 || us == 1019 || us == 1020 || us == 1021 || us == 1022 || us == 1028 || us == 1029 || us == 1034 || us == 1037 || us == 1043 ||
            us == 1044 || us == 1045 || us == 1046 || us == 1059 || us == 1064 || us == 1065 || us == 1066 || us == 1089 || us == 1092 || us == 1104 ||
            us == 1105 || us == 1113 || us == 1114 || us == 1126 || us == 1127 || us == 1129 || us == 1132 || us == 1135 || us == 1136) {
            return 13;
        }

        if (us == 1117 || us == 1152 || us == 1153 || us == 1155 || us == 1157 || us == 1160 || us == 1165 || us == 1166 || us == 1169 || us == 1170 ||
            us == 1171 || us == 1172 || us == 1173 || us == 1174 || us == 1175 || us == 1179 || us == 1181 || us == 1182 || us == 1185 || us == 1188 ||
            us == 1189 || us == 1190 || us == 1191) {
            return 14;
        }

        if (us == 1140 || us == 1141 || us == 1148 || us == 1149 || us == 1150 || us == 1151 || us == 1154 || us == 1156 || us == 1159 || us == 1161 ||
            us == 1167 || us == 1168 || us == 1176 || us == 1177 || us == 1178 || us == 1180 || us == 1183 || us == 1184 || us == 1186 || us == 1187 ||
            us == 1192 || us == 1193) {
            return 15;
        }

        if (us == 1100 || us == 1123 || us == 1125) {
            return 16;
        }

        return -1;
    }
} // namespace VantaMP::Shared
