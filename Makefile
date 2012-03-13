CC = g++-4.8.0-alpha20120304
PREFIX = /usr

ifeq (,$(TARGET))
 TARGET = taskbar
endif

	ifeq ($(BUILD),debug)
else ifeq ($(BUILD),reldbg)
else ifeq ($(BUILD),release)
else ifeq ($(BUILD),trace)
else
 BUILD = release
endif

#TODO: use dependency files (.P) to link object files .o
SRCS = core array string process vector

	 ifeq ($(TARGET),player)
 SRCS += signal stream file image window font interface alsa ffmpeg resample player
 ICONS = play pause next
 LIBS += -lasound -lavformat -lavcodec
else ifeq ($(TARGET),sampler)
 SRCS += stream time signal file alsa resample sequencer flac sampler midi music
 LIBS += -lasound
else ifeq ($(TARGET),music)
 SRCS += file image window font interface alsa resample sequencer sampler midi pdf music
 LIBS += -lasound
 INSTALL = icons/music.png music.desktop
else ifeq ($(TARGET),taskbar)
 SRCS += signal stream time file image window font interface launcher taskbar
 ICONS = button shutdown
 #ICONS = system network utility graphics office
 LIBS += -lrt
else ifeq ($(TARGET),editor)
 SRCS += file image gl window font editor
 GLSL = editor
 GPUS = shader
else ifeq ($(TARGET),symbolic)
 SRCS += symbolic algebra expression
else ifeq ($(TARGET),flac)
 SRCS += file flac codec disasm
else ifeq ($(TARGET),bspline)
 SRCS += window bspline file image
endif

ifneq (,$(findstring image,$(SRCS)))
  LIBS += -lz
endif

ifneq (,$(findstring font,$(SRCS)))
  INCLUDES = -I/usr/include/freetype2
  LIBS += -lfreetype
endif

ifneq (,$(findstring window,$(SRCS)))
  LIBS += -lX11 -lXext
endif

ifneq (,$(findstring gl,$(SRCS)))
 FLAGS += -DGL
 LIBS += -lGL
 ifeq ($(BUILD),debug)
  FLAGS += -DGLU
  LIBS += -lGLU
 endif
endif

SRCS += $(ICONS:%=icons/%)
SRCS += $(GPUS:%=%.gpu)

#-fno-implicit-templates
FLAGS += -pipe -std=c++11 -fno-operator-names -Wall -Wextra -Wno-narrowing -Wno-missing-field-initializers -fno-exceptions -fno-rtti -march=native

ifeq ($(BUILD),debug)
	FLAGS += -ggdb -DDEBUG -O2 -fno-omit-frame-pointer
	LIBS += -lbfd
else ifeq ($(BUILD),reldbg)
	FLAGS += -ggdb -Ofast
else ifeq ($(BUILD),release)
	FLAGS += -Ofast
else ifeq ($(BUILD),trace)
	FLAGS += -g -DDEBUG
	LIBS += -lbfd
	FLAGS += -finstrument-functions -finstrument-functions-exclude-file-list=intrin,vector -DTRACE
endif

all: prepare $(SRCS:%=$(BUILD)/%.o)
	$(CC) $(SRCS:%=$(BUILD)/%.o) $(LIBS) -o $(BUILD)/$(TARGET)

$(BUILD)/%.o : %.cc
	$(CC) $(FLAGS) $(INCLUDES) -c -MD -o $@ $<
	@cp $(BUILD)/$*.d $(BUILD)/$*.P; \
	sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' -e '/^$$/ d' -e 's/$$/ :/' < $(BUILD)/$*.d >> $(BUILD)/$*.P; \
	rm -f $(BUILD)/$*.d

-include $(SRCS:%=$(BUILD)/%.P)

#Build GLSL compiler frontend
$(BUILD)/glsl: string.cc file.cc glsl.cc
	$(CC) $(FLAGS) -DNO_BFD string.cc file.cc glsl.cc -lX11 -lGL -o $(BUILD)/glsl

$(BUILD)/%.gpu.o: $(GLSL).glsl $(BUILD)/glsl
	$(BUILD)/glsl $*.gpu $(GLSL).glsl $*
	ld -r -b binary -o $@ $*.gpu
	rm -f $*.gpu

$(BUILD)/%.o: %.png
	ld -r -b binary -o $@ $<

prepare:
	@mkdir -p $(BUILD)/icons
	@ln -sf $(TARGET).files serenity.files

clean:
	rm $(BUILD)/*.o

install_icons/%.png: icons/%.png
	@cp $< $(PREFIX)/share/icons/hicolor/32x32/apps

install_%.desktop: %.desktop
	@cp $< $(PREFIX)/share/applications/

install: all $(INSTALL:%=install_%)
	cp $(BUILD)/$(TARGET) $(PREFIX)/bin/$(TARGET)
