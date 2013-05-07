PREFIX ?= /usr
BUILD ?= release
ifeq ($(CC),cc)
 CC = /ptmp/gcc-4.8.0/bin/g++ -I/ptmp/include -L/ptmp/lib -pipe -march=native -std=c++11
 # CC = clang++ -Wno-lambda-extensions -march=native -std=c++11
endif
FLAGS := -funsigned-char -fno-exceptions -Wall -Wextra -Wno-missing-field-initializers
FLAGS_debug = -DDEBUG -g #-Og prevents backtrace
FLAGS_profile = -DPROFILE -g -Ofast -finstrument-functions -finstrument-functions-exclude-file-list=core,array,string,file,process,time,map,trace,profile,vector
FLAGS_fast = -g -Ofast
FLAGS_release = -Ofast
FLAGS += $(FLAGS_$(BUILD))

SRCS = $(SRCS_$(BUILD))
SRCS_profile = profile

ICONS = arrow horizontal vertical fdiagonal bdiagonal move text $(ICONS_$(TARGET))
ICONS_taskbar = button
ICONS_desktop = feeds network shutdown
ICONS_player = play pause next
ICONS_analyzer = play pause
ICONS_music = music
ICONS_test = feeds network
SRCS += $(ICONS:%=icons/%)

LIBS_time = rt
LIBS_process = pthread
FLAGS_font = -I$(STAGING_DIR)/usr/include/freetype2
LIBS_font = freetype
LIBS_http = ssl
LIBS_tiff = tiff
LIBS_ffmpeg = avutil avcodec avformat
LIBS_record = avutil avcodec avformat swscale
LIBS_asound = asound
LIBS_algebra = umfpack

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
	@python dep.py $(BUILD)/$(TARGET) $@ $(BUILD) $< >$@
    #release/dep

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

$(BUILD)/%.glsl.o: %.glsl
	@echo $<
	@test -e $(dir $@) || mkdir -p $(dir $@)
	@ld -r -b binary -o $@ $<

$(BUILD)/$(TARGET): $(SRCS:%=$(BUILD)/%.o)
	$(eval LIBS= $(filter %.o, $^))
	$(eval LIBS= $(LIBS:$(BUILD)/%.o=LIBS_%))
	$(eval LIBS= $(LIBS:%=$$(%)))
	@$(CC) $(FLAGS) $(filter %.o, $^) $(LIBS:%=-l%) -o $(BUILD)/$(TARGET)
	@echo $(BUILD)/$(TARGET)

install_icons/%.png: icons/%.png
	@cp $< $(PREFIX)/share/icons/hicolor/32x32/apps

install_%.desktop: %.desktop
	@cp $< $(PREFIX)/share/applications/

install: all $(INSTALL:%=install_%)
	@echo $(PREFIX)/bin/$(TARGET)
	@mv $(BUILD)/$(TARGET) $(PREFIX)/bin/$(TARGET)
