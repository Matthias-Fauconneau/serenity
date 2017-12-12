#pragma once
/// \file sheet.h
#include "notation.h"
#include "widget.h"
#include "font.h"
#include "midi.h"
#include "graphics.h"

inline void render(const Graphics& graphics, RenderTarget2D& target, vec2 offset=0, vec2 unused size=0) {
    offset += graphics.offset;
    for(const auto& e: graphics.blits) {
        if(uint2(e.size) == e.image.size) target.blit(round(offset+e.origin), e.size, e.image, e.color, e.opacity);
        else {
            target.blit(round(offset+e.origin), round(e.size), e.image, e.color, e.opacity);
        }
    }
    for(const auto& e: graphics.fills) target.fill(round(offset+e.origin), e.size, e.color, e.opacity);
    for(const auto& e: graphics.lines) target.line(offset+e.p0, offset+e.p1, e.color, e.opacity, e.hint);
    for(const auto& e: graphics.glyphs) {
        Font::Glyph glyph = e.font.font(e.fontSize).render(e.index);
        if(glyph.image) target.blit(round(offset+e.origin)+vec2(glyph.offset), vec2(glyph.image.size), glyph.image, e.color, e.opacity);
    }
    //for(const auto& e: graphics.parallelograms) target.parallelogram(int2(round(offset+e.min)), int2(round(offset+e.max)), e.dy, e.color, e.opacity);
    //for(const auto& e: graphics.cubics) cubic(target, e.points, e.color, e.opacity, offset);
    for(const auto& e: graphics.graphics) render(e.value, target, offset+e.key);
}

struct GraphicsWidget : Graphics, Widget {
    GraphicsWidget() {}
    GraphicsWidget(Graphics&& o) : Graphics(::move(o)) {}
    virtual ~GraphicsWidget() {}
    vec2 sizeHint(vec2) override { assert_(isNumber(bounds.max), bounds); return bounds.max; }
    void render(RenderTarget2D& target, vec2 offset=0, vec2 size=0) override { ::render(*this, target, offset, size); }
};

/// Layouts musical notations to graphic primitives
struct Sheet {
    // Graphics
    map<uint, float> measureBars; // Maps sheet time to position of measure starts
    array<Graphics> pages; // FIXME: Page[]:Line[]:Measures[]
    shared<Graphics> debug;

    int lowestStep = 0, highestStep = 0;

    // -- Control
    array<size_t> measureToChord; // First chord index of measure
    array<size_t> chordToNote; // First MIDI note index for each chord

    //float stop(vec2 unused size, int unused axis, float currentPosition, int direction) override;

    // -- MIDI Synchronization
    buffer<Sign> midiToSign; /// Sign of corresponding note for each MIDI note
    uint extraErrors = 0, missingErrors = 0, wrongErrors = 0, orderErrors = 0;
    size_t firstSynchronizationFailureChordIndex = -1;

    // -- Page layout
    int2 pageSize;

    /// Layouts musical notations to graphic primitives
    Sheet(mref<Sign> signs, uint ticksPerQuarter, int2 pageSize=0, float halfLineInterval = 4, ref<MidiNote> midiNotes={}, string title="", bool pageNumbers=false, bool measureNumbers=false);
};
