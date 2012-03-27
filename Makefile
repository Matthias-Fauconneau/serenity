CC = g++-4.8.0-alpha20120304
PREFIX ?= /usr
TARGET ?= taskbar
BUILD ?= release
FLAGS ?= -pipe -std=c++11 -Wall -Wextra -Wno-narrowing -Wno-missing-field-initializers -fno-exceptions -march=native
#-fno-operator-names

FLAGS += $(FLAGS_$(BUILD))

FLAGS_debug := -ggdb -DDEBUG -fno-omit-frame-pointer
FLAGS_fast := -ggdb -DDEBUG -fno-omit-frame-pointer -Ofast -fno-rtti
FLAGS_release := -Ofast -fno-rtti
FLAGS_trace := -g -DDEBUG -finstrument-functions -finstrument-functions-exclude-file-list=intrin,vector -DTRACE
FLAGS_memory := -ggdb -DDEBUG -Ofast -fno-omit-frame-pointer -frtti -DTRACE_MALLOC

FLAGS_font = -I/usr/include/freetype2

SRCS = $(SRCS_$(TARGET))
SRCS_player := png
SRCS_music := png
SRCS_taskbar := png
SRCS_feeds := png jpeg

LIBS_debug = bfd
LIBS_fast = bfd
LIBS_trace = bfd
LIBS_memory = bfd

LIBS_time= rt
LIBS_alsa := asound
LIBS_png := z
LIBS_http := ssl
LIBS_ffmpeg := avformat avcodec
LIBS_font := freetype
LIBS_window := X11 Xext

ICONS = $(ICONS_$(TARGET))
ICONS_player := play pause next
ICONS_feeds := feeds
ICONS_music := music music256
ICONS_taskbar := button shutdown

SRCS += $(ICONS:%=icons/%)

INSTALL = icons/$(TARGET).png $(TARGET).desktop

all: prepare $(BUILD)/$(TARGET)

%.l: %.d
	./dep.py $(BUILD)/$(TARGET) $@ $(BUILD) $< >$@

$(BUILD)/$(TARGET): $(SRCS:%=$(BUILD)/%.o)
	$(eval LIBS= $(filter %.o, $^))
	$(eval LIBS= $(LIBS:$(BUILD)/%.o=LIBS_%))
	$(eval LIBS= $(LIBS:%=$$(%)))
	$(CC) $(LIBS_$(BUILD):%=-l%) $(LIBS:%=-l%) -o $(BUILD)/$(TARGET) $(filter %.o, $^)

$(BUILD)/%.d: %.cc
	@test -e $(dir $@) || mkdir -p $(dir $@)
	$(CC) $(FLAGS) -MM -MT $(BUILD)/$*.o -MT $(BUILD)/$*.d -MF $@ $<

ifneq ($(MAKECMDGOALS),clean)
-include $(BUILD)/$(TARGET).l
endif

$(BUILD)/%.o : %.cc
	$(CC) $(FLAGS) $(FLAGS_$*) -c -o $@ $<

$(BUILD)/%.o: %.png
	@test -e $(dir $@) || mkdir -p $(dir $@)
	ld -r -b binary -o $@ $<

prepare:
	@ln -sf $(TARGET).files serenity.files

clean:
	rm -f $(BUILD)/*.l
	rm -f $(BUILD)/*.d
	rm -f $(BUILD)/*.o

install_icons/%.png: icons/%.png
	cp $< $(PREFIX)/share/icons/hicolor/32x32/apps

install_%.desktop: %.desktop
	cp $< $(PREFIX)/share/applications/

install: all $(INSTALL:%=install_%)
	cp $(BUILD)/$(TARGET) $(PREFIX)/bin/$(TARGET)
