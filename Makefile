PREFIX ?= /usr
TARGET ?= taskbar
BUILD ?= fast
#CC = g++ -pipe -march=native
CC = clang++ -pipe -march=native
FLAGS = -std=c++11 -funsigned-char -fno-threadsafe-statics -fno-exceptions -fno-rtti -Wall -Wextra -Wno-missing-field-initializers -Wno-volatile-register-var $(FLAGS_$(BUILD))
#debug: include debug symbols, keep all assertions
FLAGS_debug := -g -DDEBUG -fno-omit-frame-pointer -fno-optimize-sibling-calls
#fast: include debug symbols, disable all assertions, use medium optimizations
FLAGS_fast := -g -O2 -fno-omit-frame-pointer -fno-optimize-sibling-calls
#profile: include debug symbols, disable all assertions, use medium optimizations, instrument functions
FLAGS_profile := -g -O3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -finstrument-functions
SRCS = $(SRCS_$(BUILD))
SRCS_profile := profile
#release: strip debug symbols, disable all assertions, use heavy optimizations
FLAGS_release := -O3 -fomit-frame-pointer

ICONS = arrow horizontal vertical fdiagonal bdiagonal move $(ICONS_$(TARGET))
ICONS_taskbar := button
ICONS_desktop := feeds network shutdown
ICONS_player := play pause next
ICONS_music := music
SRCS += $(ICONS:%=icons/%)

LIBS_player = mpg123
LIBS_ffmpeg = avformat avcodec
LIBS_http = ssl
LIBS_sequencer = asound

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
	@$(CC) $(LIBS:%=-l%) -o $(BUILD)/$(TARGET) $(filter %.o, $^)
	@echo $(BUILD)/$(TARGET)

$(BUILD)/%.d: %.cc
	@test -e $(dir $@) || mkdir -p $(dir $@)
	@$(CC) $(FLAGS) -MM -MT $(BUILD)/$*.o -MT $(BUILD)/$*.d $< > $@

ifneq ($(MAKECMDGOALS),clean)
-include $(BUILD)/$(TARGET).l
endif

$(BUILD)/%.o : %.cc
	@echo $<
	@test -e $(dir $@) || mkdir -p $(dir $@)
	@$(CC) $(FLAGS) -c -o $@ $<

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
	mv $(BUILD)/$(TARGET) $(PREFIX)/bin/$(TARGET)
