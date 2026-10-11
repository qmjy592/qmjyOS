.section .text
.code64
.extern exception_handler

.macro ISR_NOERR num
.global isr\num
isr\num:
    pushq $0
    pushq $\num
    jmp isr_common
.endm
.macro ISR_ERR num
.global isr\num
isr\num:
    pushq $\num
    jmp isr_common
.endm
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR 8
ISR_NOERR 9
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR 29
ISR_ERR 30
ISR_NOERR 31

# 独立兜底入口：每个向量报告自己的真实编号。
.global isr_unexpected_34
isr_unexpected_34:
    pushq $0
    pushq $34
    jmp isr_common
.global isr_unexpected_35
isr_unexpected_35:
    pushq $0
    pushq $35
    jmp isr_common
.global isr_unexpected_36
isr_unexpected_36:
    pushq $0
    pushq $36
    jmp isr_common
.global isr_unexpected_37
isr_unexpected_37:
    pushq $0
    pushq $37
    jmp isr_common
.global isr_unexpected_38
isr_unexpected_38:
    pushq $0
    pushq $38
    jmp isr_common
.global isr_unexpected_39
isr_unexpected_39:
    pushq $0
    pushq $39
    jmp isr_common
.global isr_unexpected_40
isr_unexpected_40:
    pushq $0
    pushq $40
    jmp isr_common
.global isr_unexpected_41
isr_unexpected_41:
    pushq $0
    pushq $41
    jmp isr_common
.global isr_unexpected_42
isr_unexpected_42:
    pushq $0
    pushq $42
    jmp isr_common
.global isr_unexpected_43
isr_unexpected_43:
    pushq $0
    pushq $43
    jmp isr_common
.global isr_unexpected_44
isr_unexpected_44:
    pushq $0
    pushq $44
    jmp isr_common
.global isr_unexpected_45
isr_unexpected_45:
    pushq $0
    pushq $45
    jmp isr_common
.global isr_unexpected_46
isr_unexpected_46:
    pushq $0
    pushq $46
    jmp isr_common
.global isr_unexpected_47
isr_unexpected_47:
    pushq $0
    pushq $47
    jmp isr_common
.global isr_unexpected_48
isr_unexpected_48:
    pushq $0
    pushq $48
    jmp isr_common
.global isr_unexpected_49
isr_unexpected_49:
    pushq $0
    pushq $49
    jmp isr_common
.global isr_unexpected_50
isr_unexpected_50:
    pushq $0
    pushq $50
    jmp isr_common
.global isr_unexpected_51
isr_unexpected_51:
    pushq $0
    pushq $51
    jmp isr_common
.global isr_unexpected_52
isr_unexpected_52:
    pushq $0
    pushq $52
    jmp isr_common
.global isr_unexpected_53
isr_unexpected_53:
    pushq $0
    pushq $53
    jmp isr_common
.global isr_unexpected_54
isr_unexpected_54:
    pushq $0
    pushq $54
    jmp isr_common
.global isr_unexpected_55
isr_unexpected_55:
    pushq $0
    pushq $55
    jmp isr_common
.global isr_unexpected_56
isr_unexpected_56:
    pushq $0
    pushq $56
    jmp isr_common
.global isr_unexpected_57
isr_unexpected_57:
    pushq $0
    pushq $57
    jmp isr_common
.global isr_unexpected_58
isr_unexpected_58:
    pushq $0
    pushq $58
    jmp isr_common
.global isr_unexpected_59
isr_unexpected_59:
    pushq $0
    pushq $59
    jmp isr_common
.global isr_unexpected_60
isr_unexpected_60:
    pushq $0
    pushq $60
    jmp isr_common
.global isr_unexpected_61
isr_unexpected_61:
    pushq $0
    pushq $61
    jmp isr_common
.global isr_unexpected_62
isr_unexpected_62:
    pushq $0
    pushq $62
    jmp isr_common
.global isr_unexpected_63
isr_unexpected_63:
    pushq $0
    pushq $63
    jmp isr_common
.global isr_unexpected_64
isr_unexpected_64:
    pushq $0
    pushq $64
    jmp isr_common
.global isr_unexpected_65
isr_unexpected_65:
    pushq $0
    pushq $65
    jmp isr_common
.global isr_unexpected_66
isr_unexpected_66:
    pushq $0
    pushq $66
    jmp isr_common
.global isr_unexpected_67
isr_unexpected_67:
    pushq $0
    pushq $67
    jmp isr_common
.global isr_unexpected_68
isr_unexpected_68:
    pushq $0
    pushq $68
    jmp isr_common
.global isr_unexpected_69
isr_unexpected_69:
    pushq $0
    pushq $69
    jmp isr_common
.global isr_unexpected_70
isr_unexpected_70:
    pushq $0
    pushq $70
    jmp isr_common
.global isr_unexpected_71
isr_unexpected_71:
    pushq $0
    pushq $71
    jmp isr_common
.global isr_unexpected_72
isr_unexpected_72:
    pushq $0
    pushq $72
    jmp isr_common
.global isr_unexpected_73
isr_unexpected_73:
    pushq $0
    pushq $73
    jmp isr_common
.global isr_unexpected_74
isr_unexpected_74:
    pushq $0
    pushq $74
    jmp isr_common
.global isr_unexpected_75
isr_unexpected_75:
    pushq $0
    pushq $75
    jmp isr_common
.global isr_unexpected_76
isr_unexpected_76:
    pushq $0
    pushq $76
    jmp isr_common
.global isr_unexpected_77
isr_unexpected_77:
    pushq $0
    pushq $77
    jmp isr_common
.global isr_unexpected_78
isr_unexpected_78:
    pushq $0
    pushq $78
    jmp isr_common
.global isr_unexpected_79
isr_unexpected_79:
    pushq $0
    pushq $79
    jmp isr_common
.global isr_unexpected_80
isr_unexpected_80:
    pushq $0
    pushq $80
    jmp isr_common
.global isr_unexpected_81
isr_unexpected_81:
    pushq $0
    pushq $81
    jmp isr_common
.global isr_unexpected_82
isr_unexpected_82:
    pushq $0
    pushq $82
    jmp isr_common
.global isr_unexpected_83
isr_unexpected_83:
    pushq $0
    pushq $83
    jmp isr_common
.global isr_unexpected_84
isr_unexpected_84:
    pushq $0
    pushq $84
    jmp isr_common
.global isr_unexpected_85
isr_unexpected_85:
    pushq $0
    pushq $85
    jmp isr_common
.global isr_unexpected_86
isr_unexpected_86:
    pushq $0
    pushq $86
    jmp isr_common
.global isr_unexpected_87
isr_unexpected_87:
    pushq $0
    pushq $87
    jmp isr_common
.global isr_unexpected_88
isr_unexpected_88:
    pushq $0
    pushq $88
    jmp isr_common
.global isr_unexpected_89
isr_unexpected_89:
    pushq $0
    pushq $89
    jmp isr_common
.global isr_unexpected_90
isr_unexpected_90:
    pushq $0
    pushq $90
    jmp isr_common
.global isr_unexpected_91
isr_unexpected_91:
    pushq $0
    pushq $91
    jmp isr_common
.global isr_unexpected_92
isr_unexpected_92:
    pushq $0
    pushq $92
    jmp isr_common
.global isr_unexpected_93
isr_unexpected_93:
    pushq $0
    pushq $93
    jmp isr_common
.global isr_unexpected_94
isr_unexpected_94:
    pushq $0
    pushq $94
    jmp isr_common
.global isr_unexpected_95
isr_unexpected_95:
    pushq $0
    pushq $95
    jmp isr_common
.global isr_unexpected_96
isr_unexpected_96:
    pushq $0
    pushq $96
    jmp isr_common
.global isr_unexpected_97
isr_unexpected_97:
    pushq $0
    pushq $97
    jmp isr_common
.global isr_unexpected_98
isr_unexpected_98:
    pushq $0
    pushq $98
    jmp isr_common
.global isr_unexpected_99
isr_unexpected_99:
    pushq $0
    pushq $99
    jmp isr_common
.global isr_unexpected_100
isr_unexpected_100:
    pushq $0
    pushq $100
    jmp isr_common
.global isr_unexpected_101
isr_unexpected_101:
    pushq $0
    pushq $101
    jmp isr_common
.global isr_unexpected_102
isr_unexpected_102:
    pushq $0
    pushq $102
    jmp isr_common
.global isr_unexpected_103
isr_unexpected_103:
    pushq $0
    pushq $103
    jmp isr_common
.global isr_unexpected_104
isr_unexpected_104:
    pushq $0
    pushq $104
    jmp isr_common
.global isr_unexpected_105
isr_unexpected_105:
    pushq $0
    pushq $105
    jmp isr_common
.global isr_unexpected_106
isr_unexpected_106:
    pushq $0
    pushq $106
    jmp isr_common
.global isr_unexpected_107
isr_unexpected_107:
    pushq $0
    pushq $107
    jmp isr_common
.global isr_unexpected_108
isr_unexpected_108:
    pushq $0
    pushq $108
    jmp isr_common
.global isr_unexpected_109
isr_unexpected_109:
    pushq $0
    pushq $109
    jmp isr_common
.global isr_unexpected_110
isr_unexpected_110:
    pushq $0
    pushq $110
    jmp isr_common
.global isr_unexpected_111
isr_unexpected_111:
    pushq $0
    pushq $111
    jmp isr_common
.global isr_unexpected_112
isr_unexpected_112:
    pushq $0
    pushq $112
    jmp isr_common
.global isr_unexpected_113
isr_unexpected_113:
    pushq $0
    pushq $113
    jmp isr_common
.global isr_unexpected_114
isr_unexpected_114:
    pushq $0
    pushq $114
    jmp isr_common
.global isr_unexpected_115
isr_unexpected_115:
    pushq $0
    pushq $115
    jmp isr_common
.global isr_unexpected_116
isr_unexpected_116:
    pushq $0
    pushq $116
    jmp isr_common
.global isr_unexpected_117
isr_unexpected_117:
    pushq $0
    pushq $117
    jmp isr_common
.global isr_unexpected_118
isr_unexpected_118:
    pushq $0
    pushq $118
    jmp isr_common
.global isr_unexpected_119
isr_unexpected_119:
    pushq $0
    pushq $119
    jmp isr_common
.global isr_unexpected_120
isr_unexpected_120:
    pushq $0
    pushq $120
    jmp isr_common
.global isr_unexpected_121
isr_unexpected_121:
    pushq $0
    pushq $121
    jmp isr_common
.global isr_unexpected_122
isr_unexpected_122:
    pushq $0
    pushq $122
    jmp isr_common
.global isr_unexpected_123
isr_unexpected_123:
    pushq $0
    pushq $123
    jmp isr_common
.global isr_unexpected_124
isr_unexpected_124:
    pushq $0
    pushq $124
    jmp isr_common
.global isr_unexpected_125
isr_unexpected_125:
    pushq $0
    pushq $125
    jmp isr_common
.global isr_unexpected_126
isr_unexpected_126:
    pushq $0
    pushq $126
    jmp isr_common
.global isr_unexpected_127
isr_unexpected_127:
    pushq $0
    pushq $127
    jmp isr_common
.global isr_unexpected_128
isr_unexpected_128:
    pushq $0
    pushq $128
    jmp isr_common
.global isr_unexpected_129
isr_unexpected_129:
    pushq $0
    pushq $129
    jmp isr_common
.global isr_unexpected_130
isr_unexpected_130:
    pushq $0
    pushq $130
    jmp isr_common
.global isr_unexpected_131
isr_unexpected_131:
    pushq $0
    pushq $131
    jmp isr_common
.global isr_unexpected_132
isr_unexpected_132:
    pushq $0
    pushq $132
    jmp isr_common
.global isr_unexpected_133
isr_unexpected_133:
    pushq $0
    pushq $133
    jmp isr_common
.global isr_unexpected_134
isr_unexpected_134:
    pushq $0
    pushq $134
    jmp isr_common
.global isr_unexpected_135
isr_unexpected_135:
    pushq $0
    pushq $135
    jmp isr_common
.global isr_unexpected_136
isr_unexpected_136:
    pushq $0
    pushq $136
    jmp isr_common
.global isr_unexpected_137
isr_unexpected_137:
    pushq $0
    pushq $137
    jmp isr_common
.global isr_unexpected_138
isr_unexpected_138:
    pushq $0
    pushq $138
    jmp isr_common
.global isr_unexpected_139
isr_unexpected_139:
    pushq $0
    pushq $139
    jmp isr_common
.global isr_unexpected_140
isr_unexpected_140:
    pushq $0
    pushq $140
    jmp isr_common
.global isr_unexpected_141
isr_unexpected_141:
    pushq $0
    pushq $141
    jmp isr_common
.global isr_unexpected_142
isr_unexpected_142:
    pushq $0
    pushq $142
    jmp isr_common
.global isr_unexpected_143
isr_unexpected_143:
    pushq $0
    pushq $143
    jmp isr_common
.global isr_unexpected_144
isr_unexpected_144:
    pushq $0
    pushq $144
    jmp isr_common
.global isr_unexpected_145
isr_unexpected_145:
    pushq $0
    pushq $145
    jmp isr_common
.global isr_unexpected_146
isr_unexpected_146:
    pushq $0
    pushq $146
    jmp isr_common
.global isr_unexpected_147
isr_unexpected_147:
    pushq $0
    pushq $147
    jmp isr_common
.global isr_unexpected_148
isr_unexpected_148:
    pushq $0
    pushq $148
    jmp isr_common
.global isr_unexpected_149
isr_unexpected_149:
    pushq $0
    pushq $149
    jmp isr_common
.global isr_unexpected_150
isr_unexpected_150:
    pushq $0
    pushq $150
    jmp isr_common
.global isr_unexpected_151
isr_unexpected_151:
    pushq $0
    pushq $151
    jmp isr_common
.global isr_unexpected_152
isr_unexpected_152:
    pushq $0
    pushq $152
    jmp isr_common
.global isr_unexpected_153
isr_unexpected_153:
    pushq $0
    pushq $153
    jmp isr_common
.global isr_unexpected_154
isr_unexpected_154:
    pushq $0
    pushq $154
    jmp isr_common
.global isr_unexpected_155
isr_unexpected_155:
    pushq $0
    pushq $155
    jmp isr_common
.global isr_unexpected_156
isr_unexpected_156:
    pushq $0
    pushq $156
    jmp isr_common
.global isr_unexpected_157
isr_unexpected_157:
    pushq $0
    pushq $157
    jmp isr_common
.global isr_unexpected_158
isr_unexpected_158:
    pushq $0
    pushq $158
    jmp isr_common
.global isr_unexpected_159
isr_unexpected_159:
    pushq $0
    pushq $159
    jmp isr_common
.global isr_unexpected_160
isr_unexpected_160:
    pushq $0
    pushq $160
    jmp isr_common
.global isr_unexpected_161
isr_unexpected_161:
    pushq $0
    pushq $161
    jmp isr_common
.global isr_unexpected_162
isr_unexpected_162:
    pushq $0
    pushq $162
    jmp isr_common
.global isr_unexpected_163
isr_unexpected_163:
    pushq $0
    pushq $163
    jmp isr_common
.global isr_unexpected_164
isr_unexpected_164:
    pushq $0
    pushq $164
    jmp isr_common
.global isr_unexpected_165
isr_unexpected_165:
    pushq $0
    pushq $165
    jmp isr_common
.global isr_unexpected_166
isr_unexpected_166:
    pushq $0
    pushq $166
    jmp isr_common
.global isr_unexpected_167
isr_unexpected_167:
    pushq $0
    pushq $167
    jmp isr_common
.global isr_unexpected_168
isr_unexpected_168:
    pushq $0
    pushq $168
    jmp isr_common
.global isr_unexpected_169
isr_unexpected_169:
    pushq $0
    pushq $169
    jmp isr_common
.global isr_unexpected_170
isr_unexpected_170:
    pushq $0
    pushq $170
    jmp isr_common
.global isr_unexpected_171
isr_unexpected_171:
    pushq $0
    pushq $171
    jmp isr_common
.global isr_unexpected_172
isr_unexpected_172:
    pushq $0
    pushq $172
    jmp isr_common
.global isr_unexpected_173
isr_unexpected_173:
    pushq $0
    pushq $173
    jmp isr_common
.global isr_unexpected_174
isr_unexpected_174:
    pushq $0
    pushq $174
    jmp isr_common
.global isr_unexpected_175
isr_unexpected_175:
    pushq $0
    pushq $175
    jmp isr_common
.global isr_unexpected_176
isr_unexpected_176:
    pushq $0
    pushq $176
    jmp isr_common
.global isr_unexpected_177
isr_unexpected_177:
    pushq $0
    pushq $177
    jmp isr_common
.global isr_unexpected_178
isr_unexpected_178:
    pushq $0
    pushq $178
    jmp isr_common
.global isr_unexpected_179
isr_unexpected_179:
    pushq $0
    pushq $179
    jmp isr_common
.global isr_unexpected_180
isr_unexpected_180:
    pushq $0
    pushq $180
    jmp isr_common
.global isr_unexpected_181
isr_unexpected_181:
    pushq $0
    pushq $181
    jmp isr_common
.global isr_unexpected_182
isr_unexpected_182:
    pushq $0
    pushq $182
    jmp isr_common
.global isr_unexpected_183
isr_unexpected_183:
    pushq $0
    pushq $183
    jmp isr_common
.global isr_unexpected_184
isr_unexpected_184:
    pushq $0
    pushq $184
    jmp isr_common
.global isr_unexpected_185
isr_unexpected_185:
    pushq $0
    pushq $185
    jmp isr_common
.global isr_unexpected_186
isr_unexpected_186:
    pushq $0
    pushq $186
    jmp isr_common
.global isr_unexpected_187
isr_unexpected_187:
    pushq $0
    pushq $187
    jmp isr_common
.global isr_unexpected_188
isr_unexpected_188:
    pushq $0
    pushq $188
    jmp isr_common
.global isr_unexpected_189
isr_unexpected_189:
    pushq $0
    pushq $189
    jmp isr_common
.global isr_unexpected_190
isr_unexpected_190:
    pushq $0
    pushq $190
    jmp isr_common
.global isr_unexpected_191
isr_unexpected_191:
    pushq $0
    pushq $191
    jmp isr_common
.global isr_unexpected_192
isr_unexpected_192:
    pushq $0
    pushq $192
    jmp isr_common
.global isr_unexpected_193
isr_unexpected_193:
    pushq $0
    pushq $193
    jmp isr_common
.global isr_unexpected_194
isr_unexpected_194:
    pushq $0
    pushq $194
    jmp isr_common
.global isr_unexpected_195
isr_unexpected_195:
    pushq $0
    pushq $195
    jmp isr_common
.global isr_unexpected_196
isr_unexpected_196:
    pushq $0
    pushq $196
    jmp isr_common
.global isr_unexpected_197
isr_unexpected_197:
    pushq $0
    pushq $197
    jmp isr_common
.global isr_unexpected_198
isr_unexpected_198:
    pushq $0
    pushq $198
    jmp isr_common
.global isr_unexpected_199
isr_unexpected_199:
    pushq $0
    pushq $199
    jmp isr_common
.global isr_unexpected_200
isr_unexpected_200:
    pushq $0
    pushq $200
    jmp isr_common
.global isr_unexpected_201
isr_unexpected_201:
    pushq $0
    pushq $201
    jmp isr_common
.global isr_unexpected_202
isr_unexpected_202:
    pushq $0
    pushq $202
    jmp isr_common
.global isr_unexpected_203
isr_unexpected_203:
    pushq $0
    pushq $203
    jmp isr_common
.global isr_unexpected_204
isr_unexpected_204:
    pushq $0
    pushq $204
    jmp isr_common
.global isr_unexpected_205
isr_unexpected_205:
    pushq $0
    pushq $205
    jmp isr_common
.global isr_unexpected_206
isr_unexpected_206:
    pushq $0
    pushq $206
    jmp isr_common
.global isr_unexpected_207
isr_unexpected_207:
    pushq $0
    pushq $207
    jmp isr_common
.global isr_unexpected_208
isr_unexpected_208:
    pushq $0
    pushq $208
    jmp isr_common
.global isr_unexpected_209
isr_unexpected_209:
    pushq $0
    pushq $209
    jmp isr_common
.global isr_unexpected_210
isr_unexpected_210:
    pushq $0
    pushq $210
    jmp isr_common
.global isr_unexpected_211
isr_unexpected_211:
    pushq $0
    pushq $211
    jmp isr_common
.global isr_unexpected_212
isr_unexpected_212:
    pushq $0
    pushq $212
    jmp isr_common
.global isr_unexpected_213
isr_unexpected_213:
    pushq $0
    pushq $213
    jmp isr_common
.global isr_unexpected_214
isr_unexpected_214:
    pushq $0
    pushq $214
    jmp isr_common
.global isr_unexpected_215
isr_unexpected_215:
    pushq $0
    pushq $215
    jmp isr_common
.global isr_unexpected_216
isr_unexpected_216:
    pushq $0
    pushq $216
    jmp isr_common
.global isr_unexpected_217
isr_unexpected_217:
    pushq $0
    pushq $217
    jmp isr_common
.global isr_unexpected_218
isr_unexpected_218:
    pushq $0
    pushq $218
    jmp isr_common
.global isr_unexpected_219
isr_unexpected_219:
    pushq $0
    pushq $219
    jmp isr_common
.global isr_unexpected_220
isr_unexpected_220:
    pushq $0
    pushq $220
    jmp isr_common
.global isr_unexpected_221
isr_unexpected_221:
    pushq $0
    pushq $221
    jmp isr_common
.global isr_unexpected_222
isr_unexpected_222:
    pushq $0
    pushq $222
    jmp isr_common
.global isr_unexpected_223
isr_unexpected_223:
    pushq $0
    pushq $223
    jmp isr_common
.global isr_unexpected_224
isr_unexpected_224:
    pushq $0
    pushq $224
    jmp isr_common
.global isr_unexpected_225
isr_unexpected_225:
    pushq $0
    pushq $225
    jmp isr_common
.global isr_unexpected_226
isr_unexpected_226:
    pushq $0
    pushq $226
    jmp isr_common
.global isr_unexpected_227
isr_unexpected_227:
    pushq $0
    pushq $227
    jmp isr_common
.global isr_unexpected_228
isr_unexpected_228:
    pushq $0
    pushq $228
    jmp isr_common
.global isr_unexpected_229
isr_unexpected_229:
    pushq $0
    pushq $229
    jmp isr_common
.global isr_unexpected_230
isr_unexpected_230:
    pushq $0
    pushq $230
    jmp isr_common
.global isr_unexpected_231
isr_unexpected_231:
    pushq $0
    pushq $231
    jmp isr_common
.global isr_unexpected_232
isr_unexpected_232:
    pushq $0
    pushq $232
    jmp isr_common
.global isr_unexpected_233
isr_unexpected_233:
    pushq $0
    pushq $233
    jmp isr_common
.global isr_unexpected_234
isr_unexpected_234:
    pushq $0
    pushq $234
    jmp isr_common
.global isr_unexpected_235
isr_unexpected_235:
    pushq $0
    pushq $235
    jmp isr_common
.global isr_unexpected_236
isr_unexpected_236:
    pushq $0
    pushq $236
    jmp isr_common
.global isr_unexpected_237
isr_unexpected_237:
    pushq $0
    pushq $237
    jmp isr_common
.global isr_unexpected_238
isr_unexpected_238:
    pushq $0
    pushq $238
    jmp isr_common
.global isr_unexpected_239
isr_unexpected_239:
    pushq $0
    pushq $239
    jmp isr_common
.global isr_unexpected_240
isr_unexpected_240:
    pushq $0
    pushq $240
    jmp isr_common
.global isr_unexpected_241
isr_unexpected_241:
    pushq $0
    pushq $241
    jmp isr_common
.global isr_unexpected_242
isr_unexpected_242:
    pushq $0
    pushq $242
    jmp isr_common
.global isr_unexpected_243
isr_unexpected_243:
    pushq $0
    pushq $243
    jmp isr_common
.global isr_unexpected_244
isr_unexpected_244:
    pushq $0
    pushq $244
    jmp isr_common
.global isr_unexpected_245
isr_unexpected_245:
    pushq $0
    pushq $245
    jmp isr_common
.global isr_unexpected_246
isr_unexpected_246:
    pushq $0
    pushq $246
    jmp isr_common
.global isr_unexpected_247
isr_unexpected_247:
    pushq $0
    pushq $247
    jmp isr_common
.global isr_unexpected_248
isr_unexpected_248:
    pushq $0
    pushq $248
    jmp isr_common
.global isr_unexpected_249
isr_unexpected_249:
    pushq $0
    pushq $249
    jmp isr_common
.global isr_unexpected_250
isr_unexpected_250:
    pushq $0
    pushq $250
    jmp isr_common
.global isr_unexpected_251
isr_unexpected_251:
    pushq $0
    pushq $251
    jmp isr_common
.global isr_unexpected_252
isr_unexpected_252:
    pushq $0
    pushq $252
    jmp isr_common
.global isr_unexpected_253
isr_unexpected_253:
    pushq $0
    pushq $253
    jmp isr_common
.global isr_unexpected_254
isr_unexpected_254:
    pushq $0
    pushq $254
    jmp isr_common
.global isr_unexpected_255
isr_unexpected_255:
    pushq $0
    pushq $255
    jmp isr_common

isr_common:
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15
    mov 120(%rsp), %rdi
    mov 128(%rsp), %rsi
    mov 136(%rsp), %rdx
    mov %rsp, %r12
    andq $-16, %rsp
    call exception_handler
    mov %r12, %rsp
1:  cli
    hlt
    jmp 1b
