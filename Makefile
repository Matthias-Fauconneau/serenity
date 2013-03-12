PREFIX ?= /usr
BUILD ?= debug
CC := g++ -pipe -std=c++11 -funsigned-char -fno-exceptions -fno-rtti -Wall -Wextra -Wno-missing-field-initializers
#CC := clang++ -Wno-lambda-extensions -pipe -std=c++11 -funsigned-char -fno-exceptions -fno-rtti -Wall -Wextra -Wno-missing-field-initializers
FLAGS_debug = -DDEBUG -g
FLAGS_profile = -DPROFILE -g -O3 -finstrument-functions -finstrument-functions-exclude-file-list=core,array,string,file,process,time,map,trace,profile
FLAGS_release = -O3
CC += -march=native $(FLAGS_$(BUILD))
FLAGS_font = -I/usr/include/freetype2

ICONS = arrow horizontal vertical fdiagonal bdiagonal move text $(ICONS_$(TARGET))
ICONS_taskbar = button
ICONS_desktop = feeds network shutdown
ICONS_player = play pause next
ICONS_analyzer = play pause
ICONS_music = music
ICONS_test = feeds network

 SHADERS = display $(SHADERS_$(TARGET))
# SHADERS_blender = blender

SRCS = $(SRCS_$(BUILD)) $(ICONS:%=icons/%) $(SHADERS:%=%.glsl)
SRCS_profile = profile

LIBS_time = rt
LIBS_process = pthread
LIBS_font = freetype
LIBS_http = ssl
LIBS_gl = X11 GL
LIBS_ffmpeg = avformat avcodec
LIBS_record = swscale avformat avcodec
LIBS_asound = asound
# LIBS_sampler = fftw3f_threads
LIBS_window = Xau
LIBS_spectrogram = fftw3f_threads
LIBS_stretch = rubberband

INSTALL = $(INSTALL_$(TARGET))
INSTALL_player = icons/$(TARGET).png $(TARGET).desktop
INSTALL_feeds = icons/$(TARGET).png $(TARGET).desktop
INSTALL_music = icons/$(TARGET).png $(TARGET).desktop
INSTALL_monitor = $(TARGET).desktop

all: $(BUILD)/$(TARGET)

clean:
	@rm -f $(BUILD)/*.l
	@rm -f $(BUILD)/*.d
	@rm -f $(BUILD)/*.o
	@rm -f $(BUILD)/$(TARGET)
	@rm -fR $(BUILD)/icons
	@rmdir $(BUILD)

%.l: %.d
	@python3 dep.py $(BUILD)/$(TARGET) $@ $(BUILD) $< >$@

ifneq ($(MAKECMDGOALS),clean)
-include $(BUILD)/$(TARGET).l
endif

$(BUILD)/%.d: %.cc
	@test -e $(dir $@) || mkdir -p $(dir $@)
	@$(CC) $(FLAGS_$*) -MM -MT $(BUILD)/$*.o -MT $(BUILD)/$*.d $< > $@

$(BUILD)/%.o : %.cc
	@echo $<
	@test -e $(dir $@) || mkdir -p $(dir $@)
	@$(CC) $(FLAGS_$*) -c -o $@ $<

$(BUILD)/%.o: %.png
	@echo $<
	@test -e $(dir $@) || mkdir -p $(dir $@)
	@ld -r -b binary -o $@ $<

$(BUILD)/%.glsl.o: %.glsl
	@echo $<
	@test -e $(dir $@) || mkdir -p $(dir $@)
	@ld -r -b binary -o $@ $<

$(BUILD)/$(TARGET): $(SRCS:%=$(BUILD)/%.o)
	$(eval LIBS= $(filter %.o, $^))
	$(eval LIBS= $(LIBS:$(BUILD)/%.o=LIBS_%))
	$(eval LIBS= $(LIBS:%=$$(%)))
	@$(CC) $(filter %.o, $^) $(LIBS:%=-l%) -o $(BUILD)/$(TARGET)
	@echo $(BUILD)/$(TARGET)

install_icons/%.png: icons/%.png
	@cp $< $(PREFIX)/share/icons/hicolor/32x32/apps

install_%.desktop: %.desktop
	@cp $< $(PREFIX)/share/applications/

install: all $(INSTALL:%=install_%)
	@echo $(PREFIX)/bin/$(TARGET)
	@mv $(BUILD)/$(TARGET) $(PREFIX)/bin/$(TARGET)
