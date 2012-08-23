PREFIX ?= /usr
TARGET ?= player
BUILD ?= release

CC = clang -pipe -std=c++11 -march=native -funsigned-char -fno-threadsafe-statics -fno-exceptions -fno-rtti -Wall -Wextra -Wno-missing-field-initializers
CC += $(FLAGS_$(BUILD))
FLAGS_debug := -g -DDEBUG -fno-omit-frame-pointer
FLAGS_release := -O3 -fomit-frame-pointer
FLAGS_profile := -g -O -finstrument-functions

SRCS = $(SRCS_$(BUILD)) $(SRCS_$(TARGET))
SRCS_profile += profile
SRCS_browser += png inflate jpeg ico
SRCS_desktop += png inflate jpeg ico
SRCS_taskbar += png inflate
SRCS_player += png inflate
SRCS_music += png inflate
SRCS_test += png inflate

ICONS = arrow horizontal vertical fdiagonal bdiagonal move $(ICONS_$(TARGET))
ICONS_taskbar := button
ICONS_desktop := shutdown network
ICONS_player := play pause next
ICONS_music := music
SRCS += $(ICONS:%=icons/%)

LIBS_mpg123 = mpg123
LIBS_ffmpeg = avformat avcodec

INSTALL = $(INSTALL_$(TARGET))
INSTALL_player = icons/$(TARGET).png $(TARGET).desktop
INSTALL_feeds = icons/$(TARGET).png $(TARGET).desktop
INSTALL_music = icons/$(TARGET).png $(TARGET).desktop

all: prepare $(BUILD)/$(TARGET)

%.l: %.d
	@python dep.py $(BUILD)/$(TARGET) $@ $(BUILD) $< >$@

$(BUILD)/$(TARGET): $(SRCS:%=$(BUILD)/%.o)
	$(eval LIBS= $(filter %.o, $^))
	$(eval LIBS= $(LIBS:$(BUILD)/%.o=LIBS_%))
	$(eval LIBS= $(LIBS:%=$$(%)))
	@clang++ $(LIBS:%=-l%) -o $(BUILD)/$(TARGET) $(filter %.o, $^)
	@echo $(BUILD)/$(TARGET)

$(BUILD)/%.d: %.cc
	@test -e $(dir $@) || mkdir -p $(dir $@)
	@$(CC) -MM -MT $(BUILD)/$*.o -MT $(BUILD)/$*.d $< > $@

ifneq ($(MAKECMDGOALS),clean)
-include $(BUILD)/$(TARGET).l
endif

$(BUILD)/%.o : %.cc
	@echo $<
	@test -e $(dir $@) || mkdir -p $(dir $@)
	@$(CC) -c -o $@ $<

$(BUILD)/%.o: %.png
	@echo $<
	@test -e $(dir $@) || mkdir -p $(dir $@)
	@ld -r -b binary -o $@ $<

prepare:
	@ln -sf $(TARGET).files serenity.files

clean:
	@rm -f $(BUILD)/*.l
	@rm -f $(BUILD)/*.d
	@rm -f $(BUILD)/*.o
	@rm -f $(BUILD)/$(TARGET)
	@rm -fR $(BUILD)/icons
	@rmdir $(BUILD)

install_icons/%.png: icons/%.png
	cp $< $(PREFIX)/share/icons/hicolor/32x32/apps

install_%.desktop: %.desktop
	cp $< $(PREFIX)/share/applications/

install: all $(INSTALL:%=install_%)
	cp $(BUILD)/$(TARGET) $(PREFIX)/bin/$(TARGET)
