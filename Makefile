PREFIX ?= /usr
TARGET ?= test
BUILD ?= release

ifeq ($(CC),cc)
 #CC := g++ -fabi-version=0
 CC := clang++ -Wno-lambda-extensions
endif

FLAGS_debug = -g -fno-omit-frame-pointer -DDEBUG
FLAGS_fast = -O -g -fno-omit-frame-pointer -DDEBUG
FLAGS_profile = -g -O3 -finstrument-functions
FLAGS_release = -O3

FLAGS_editor = -DGL
FLAGS_blender = -DGL

FLAGS_font = -I/usr/include/freetype2

CC += -pipe -std=c++11 -funsigned-char -fno-threadsafe-statics -fno-exceptions -fno-rtti -Wall -Wextra -Wno-missing-field-initializers $(FLAGS_$(BUILD)) $(FLAGS_$(TARGET))
#CC += -Wno-volatile-register-var
CC += -march=native
#CC += -march=armv7-a -mtune=cortex-a8 -mfpu=neon -mfloat-abi=hard

ICONS = arrow horizontal vertical fdiagonal bdiagonal move text $(ICONS_$(TARGET))
ICONS_taskbar = button
ICONS_desktop = feeds network shutdown
ICONS_player = play pause next
ICONS_analyzer = play pause
ICONS_music = music
ICONS_test = feeds network

SHADERS = $(SHADERS_$(TARGET))
SHADERS_editor = display editor
SHADERS_blender = display blender

SRCS = $(SRCS_$(BUILD)) $(ICONS:%=icons/%) $(SHADERS:%=%.glsl)
SRCS_profile = profile

LIBS_time = rt
LIBS_process = pthread
LIBS_font = freetype
LIBS_http = ssl
LIBS_gl = X11 GL
LIBS_ffmpeg = avformat avcodec
LIBS_record = swscale avformat avcodec
LIBS_sampler = fftw3f_threads
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
	@g++ $(LIBS:%=-l%) -o $(BUILD)/$(TARGET) $(filter %.o, $^)
	@echo $(BUILD)/$(TARGET)

install_icons/%.png: icons/%.png
	@cp $< $(PREFIX)/share/icons/hicolor/32x32/apps

install_%.desktop: %.desktop
	@cp $< $(PREFIX)/share/applications/

install: all $(INSTALL:%=install_%)
	@mv $(BUILD)/$(TARGET) $(PREFIX)/bin/$(TARGET)
