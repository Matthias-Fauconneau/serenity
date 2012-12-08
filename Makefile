PREFIX ?= /usr
TARGET ?= test
BUILD ?= release

ifeq ($(CC),cc)
 ifeq ($(TARGET),music)
  CC := g++ -fabi-version=0
 else ifeq ($(TARGET),editor)
   CC := g++ -fabi-version=0
 else
  CC := clang++ -Wno-lambda-extensions
 endif
endif

FLAGS = -std=c++11 -funsigned-char -fno-threadsafe-statics -fno-exceptions -fno-rtti -Wall -Wextra -Wno-missing-field-initializers -Wno-volatile-register-var -pipe -march=native $(FLAGS_$(BUILD))
FLAGS_debug = -g -fno-omit-frame-pointer -DDEBUG
FLAGS_fast = -O -g -fno-omit-frame-pointer -DDEBUG
FLAGS_profile = -g -O3 -finstrument-functions
FLAGS_release = -O3
FLAGS_font = -I/usr/include/freetype2
FLAGS_reverb =  -I/usr/include/libfreeverb3-2/

ICONS = arrow horizontal vertical fdiagonal bdiagonal move text $(ICONS_$(TARGET))
ICONS_taskbar = button
ICONS_desktop = feeds network shutdown
ICONS_player = play pause next
ICONS_music = music

SHADERS = $(SHADERS_$(TARGET)) fill blit
SHADERS_editor = shader resolve

SRCS = $(SRCS_$(BUILD)) $(ICONS:%=icons/%) $(SHADERS:%=%.vert) $(SHADERS:%=%.frag)
SRCS_profile = profile

LIBS_time = rt
LIBS_process = pthread
LIBS_font = freetype
LIBS_http = ssl
LIBS_player = avformat avcodec
LIBS_gl = GL
LIBS_window = X11
LIBS_sampler = fftw3f_threads
LIBS_music = swscale avformat

INSTALL = $(INSTALL_$(TARGET))
INSTALL_player = icons/$(TARGET).png $(TARGET).desktop
INSTALL_feeds = icons/$(TARGET).png $(TARGET).desktop
INSTALL_music = icons/$(TARGET).png $(TARGET).desktop
INSTALL_monitor = $(TARGET).desktop

all: prepare $(BUILD)/$(TARGET)

clean:
	@rm -f $(BUILD)/*.l
	@rm -f $(BUILD)/*.d
	@rm -f $(BUILD)/*.o
	@rm -f $(BUILD)/$(TARGET)
	@rm -fR $(BUILD)/icons
	@rmdir $(BUILD)

prepare:
	@ln -sf $(TARGET).files serenity.files

%.l: %.d
	@python dep.py $(BUILD)/$(TARGET) $@ $(BUILD) $< >$@

ifneq ($(MAKECMDGOALS),clean)
-include $(BUILD)/$(TARGET).l
endif

$(BUILD)/%.d: %.cc
	@test -e $(dir $@) || mkdir -p $(dir $@)
	@$(CC) $(FLAGS) $(FLAGS_$*) -MM -MT $(BUILD)/$*.o -MT $(BUILD)/$*.d $< > $@

$(BUILD)/%.o : %.cc
	@echo $<
	@test -e $(dir $@) || mkdir -p $(dir $@)
	@$(CC) $(FLAGS) $(FLAGS_$*) -c -o $@ $<

$(BUILD)/%.o: %.png
	@echo $<
	@test -e $(dir $@) || mkdir -p $(dir $@)
	@ld -r -b binary -o $@ $<

$(BUILD)/%.vert.o: %.vert
	@echo $<
	@test -e $(dir $@) || mkdir -p $(dir $@)
	@ld -r -b binary -o $@ $<

$(BUILD)/%.frag.o: %.frag
	@echo $<
	@test -e $(dir $@) || mkdir -p $(dir $@)
	@ld -r -b binary -o $@ $<

$(BUILD)/$(TARGET): $(SRCS:%=$(BUILD)/%.o)
	$(eval LIBS= $(filter %.o, $^))
	$(eval LIBS= $(LIBS:$(BUILD)/%.o=LIBS_%))
	$(eval LIBS= $(LIBS:%=$$(%)))
	@$(CC) $(LFLAGS) $(LIBS:%=-l%) -o $(BUILD)/$(TARGET) $(filter %.o, $^)
	@echo $(BUILD)/$(TARGET)

install_icons/%.png: icons/%.png
	cp $< $(PREFIX)/share/icons/hicolor/32x32/apps

install_%.desktop: %.desktop
	cp $< $(PREFIX)/share/applications/

install: all $(INSTALL:%=install_%)
	mv $(BUILD)/$(TARGET) $(PREFIX)/bin/$(TARGET)
