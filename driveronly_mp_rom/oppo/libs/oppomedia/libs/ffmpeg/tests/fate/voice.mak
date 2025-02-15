FATE_G722 += fate-g722dec-1
fate-g722dec-1: CMD = framecrc -i $(SAMPLES)/g722/conf-adminmenu-162.g722

FATE_G722 += fate-g722-encode
fate-g722-encode: tests/data/asynth-16000-1.wav
fate-g722-encode: SRC = tests/data/asynth-16000-1.wav
fate-g722-encode: CMD = enc_dec_pcm wav md5 s16le $(SRC) -c:a g722

FATE_VOICE += $(FATE_G722)
fate-g722: $(FATE_G722)

FATE_G726 += fate-g726-encode-2bit
fate-g726-encode-2bit: tests/data/asynth-8000-1.wav
fate-g726-encode-2bit: SRC = tests/data/asynth-8000-1.wav
fate-g726-encode-2bit: CMD = enc_dec_pcm wav md5 s16le $(SRC) -c:a g726 -b:a 16k

FATE_G726 += fate-g726-encode-3bit
fate-g726-encode-3bit: tests/data/asynth-8000-1.wav
fate-g726-encode-3bit: SRC = tests/data/asynth-8000-1.wav
fate-g726-encode-3bit: CMD = enc_dec_pcm wav md5 s16le $(SRC) -c:a g726 -b:a 24k

FATE_G726 += fate-g726-encode-4bit
fate-g726-encode-4bit: tests/data/asynth-8000-1.wav
fate-g726-encode-4bit: SRC = tests/data/asynth-8000-1.wav
fate-g726-encode-4bit: CMD = enc_dec_pcm wav md5 s16le $(SRC) -c:a g726 -b:a 32k

FATE_G726 += fate-g726-encode-5bit
fate-g726-encode-5bit: tests/data/asynth-8000-1.wav
fate-g726-encode-5bit: SRC = tests/data/asynth-8000-1.wav
fate-g726-encode-5bit: CMD = enc_dec_pcm wav md5 s16le $(SRC) -c:a g726 -b:a 40k

FATE_VOICE += $(FATE_G726)
fate-g726: $(FATE_G726)

FATE_GSM += fate-gsm-ms
fate-gsm-ms: CMD = framecrc -i $(SAMPLES)/gsm/ciao.wav

FATE_GSM += fate-gsm-toast
fate-gsm-toast: CMD = framecrc -i $(SAMPLES)/gsm/sample-gsm-8000.mov -t 10

FATE_VOICE += $(FATE_GSM)
fate-gsm: $(FATE_GSM)

FATE_VOICE += fate-qcelp
fate-qcelp: CMD = pcm -i $(SAMPLES)/qcp/0036580847.QCP
fate-qcelp: CMP = oneoff
fate-qcelp: REF = $(SAMPLES)/qcp/0036580847.pcm

FATE_VOICE += fate-truespeech
fate-truespeech: CMD = pcm -i $(SAMPLES)/truespeech/a6.wav
fate-truespeech: CMP = oneoff
fate-truespeech: REF = $(SAMPLES)/truespeech/a6.pcm

FATE_SAMPLES_FFMPEG += $(FATE_VOICE)
fate-voice: $(FATE_VOICE)
