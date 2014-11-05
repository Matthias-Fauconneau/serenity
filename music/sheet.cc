#include "sheet.h"
#include "notation.h"
#include "utf8.h"

float Sheet::glyph(vec2 origin, const string name, Font& font) {
	uint index = font.index(name);
	//const Font::Glyph& glyph = font.render(index);
	//assert_(glyph.image);
	//notation.blits.append(position+vec2(glyph.offset), vec2(glyph.image.size), share(glyph.image));
	notation.glyphs.append(origin, font, index);
	return font.metrics(index).advance;
}

uint Sheet::text(vec2 origin, const string& text, Font& font, array<Glyph>& glyphs) {
    for(uint code: toUCS4(text)) {
		//const Font::Glyph& glyph = font.render(index);
		//assert_(glyph.image);
		//blits.append( Blit{vec2(int2(x, position.y)+glyph.offset), vec2(glyph.image.size), share(glyph.image)} );
		uint index = font.index(code);
		glyphs.append(origin, font, index);
		origin.x += font.metrics(index).advance;
    }
	return origin.x;
}

// Layouts notations to graphic primitives (and parses notes to MIDI keys)
Sheet::Sheet(const ref<Sign>& signs, uint divisions, uint height) { // Time steps per measure
    int x = 0;
    map<uint, Clef> clefs; KeySignature keySignature={0}; TimeSignature timeSignature={0,0};
    typedef array<Sign> Chord; // Signs belonging to a same chord (same time)
    Chord chords[2]; // Current chord (per staff)
    array<Chord> beams[2]; // Chords belonging to current beam (per staff) (also for correct single chord layout)
    array<array<Sign>> pendingSlurs[2];
    array<Sign> slurs[2]; // Signs belonging to current slur (pending slurs per staff)
    uint pedalStart = 0; // Last pedal start/change position
    Sign wedgeStart; // Current wedge
    struct Position { // Holds positions for both notes (default) and directions (explicit)
        int direction, note;
        Position(int x) : direction(x), note(x) {}
        void operator=(int x) { note=max(note,x); }
        operator int() const { return note; }
    };
    map<uint, Position> timeTrack; // Maps times to positions
    auto X = [&](const Sign& sign) { return timeTrack.at(sign.time); };
    auto P = [&](const Sign& sign) { return int2(X(sign),Y(sign)); };
    // System
    vec2 p0 = vec2(x+noteSize.x, staffY(0, 0));
    vec2 p1 = vec2(x+noteSize.x, staffY(1, -8));
    vec2 pM = vec2(x, (p0+p1).y/2);
    vec2 c0[2] = {vec2(pM.x, p0.y), vec2((pM+p0).x/2, p0.y)};
    vec2 c0M[2] = {vec2((pM+p0).x/2, pM.y), vec2(p0.x, pM.y)};
    vec2 c1M[2] = {vec2((pM+p1).x/2, pM.y), vec2(p1.x, pM.y)};
    vec2 c1[2] = {vec2(pM.x, p1.y), vec2((pM+p1).x/2, p1.y)};
	cubics.append( copyRef(ref<vec2>({p0,c0[0],c0M[0],pM,c1M[0],c1[0],p1,c1[1],c1M[1],pM,c0M[1],c0[1]})) );
    x += noteSize.x;
	{vec2 min(x-1, staffY(0, 0)), max(1, staffY(1, -8));
	notation.fills.append(min, max-min);}
	measures.append( x );
	measureToChord.append( 0 );
    for(Sign sign: signs) {
        // Layout accidentals
        Chord& chord = chords[sign.staff];
        if(chord && sign.time != chord.last().time) {
            int lastY = -1000, dx = 0;
            for(Sign sign: chord.reverse()) {
                if(!sign.note.accidental) continue;
                int y = Y(sign);
                if(abs(y-lastY)<=lineInterval) dx -= noteSize.x/2;
                else dx = 0;
                String name = "accidentals."_+ref<string>({"flat"_,"sharp"_,"natural"_})[sign.note.accidental-1];
				glyph(vec2(X(sign)-glyphSize(name).x+dx, y), name);
                lastY = y;
            }
            chord.clear();
        }

        // Layout tails and beams
        array<Chord>& beam = beams[sign.staff];
        if(beam && (
                    (sign.time%(timeSignature.beats*divisions) == (timeSignature.beats*divisions)/2 && sign.time>beam[0][0].time) || // Beam at half measure
                    (beam[0][0].time%divisions && sign.time>beam[0][0].time) || // Off beat (stem after complete chord)
                    (sign.type == Sign::Rest || sign.type == Sign::Measure) ||
                    (sign.type == Sign::Note && (beam.last().last().note.duration< Eighth || sign.note.duration<Eighth) && sign.time>beam[0][0].time))) {
            int sum = 0, count=0; for(const Chord& chord: beam) { for(Sign sign: chord) sum += clefStep(sign.note.clef.clefSign, sign.note.step); count+=chord.size; }
            bool stemUp = sum < -4*count; // sum/count<-4 (Average note height below mid staff)
            int dx = (stemUp ? noteSize.x - 2 : 0);
            int dy = (stemUp ? 0 : 0);

            if(beam.size==1) { // Draws single stem
                Sign sign = stemUp ? beam[0].last() : beam[0].first();
                int x = X(sign) + dx;
                int yMin = Y(sign.note.clef, sign.staff, beam[0].first().note.step);
                int yMax = Y(sign.note.clef, sign.staff, beam[0].last().note.step);
                int yBase = stemUp ? yMin : yMax + dy;
				int yStem = stemUp ? min(yMax-stemLength, staffY(sign.staff, -4)) : max(yMin+stemLength, staffY(sign.staff, -4));
				{vec2 min (x, ::min(yBase, yStem)), max(x+stemWidth, ::max(yBase, yStem));
					notation.fills.append(min, max-min); }
				/**/ if(sign.note.duration==Eighth) glyph(vec2(x+stemWidth, yStem), stemUp?"flags.u3"_:"flags.d3"_);
				else if(sign.note.duration==Sixteenth) glyph(vec2(x+stemWidth, yStem), stemUp?"flags.u4"_:"flags.d4"_);
            } else if(beam.size==2) { // Draws slanted beam
				float x[2], base[2], tip[2];
                for(uint i: range(2)) {
                    const Chord& chord = beam[i];
                    Sign sign = chord.first();
                    x[i] = X(sign) + dx;
                    base[i] = Y(sign.note.clef, sign.staff, (stemUp?chord.first():chord.last()).note.step) + dy;
                    tip[i] = Y(sign.note.clef, sign.staff, (stemUp?chord.last():chord.first()).note.step)+(stemUp?-1:1)*(stemLength-noteSize.y/2);
                }
				float farTip = stemUp ? min(tip[0],tip[1]) : max(tip[0],tip[1]);
				float delta[2] = {clip(-lineInterval, tip[0]-farTip, lineInterval), clip(-lineInterval, tip[1]-farTip, lineInterval)};
                farTip = stemUp ? min(farTip, staffY(sign.staff, -4)) : max(farTip, staffY(sign.staff, -4));
				for(uint i: range(2)) {
					vec2 min(x[i], ::min(base[i],farTip+delta[i])), max(x[i]+stemWidth, ::max(base[i],farTip+delta[i]));
					notation.fills.append(min, max-min);
				}
                Sign sign[2] = { stemUp?beam.first().last():beam.first().first(), stemUp?beam.last().last():beam.last().first()};
				vec2 p0 (X(sign[0])+dx, farTip+delta[0]-beamWidth/2);
				vec2 p1 (X(sign[1])+dx+stemWidth, farTip+delta[1]-beamWidth/2);
				parallelograms.append(p0, p1, beamWidth);
            } else { // Draws horizontal beam
				float stemY = stemUp ? -inf : inf;
                if(stemUp) {
                    for(const Chord& chord: beam) for(Sign sign: chord) stemY = max(stemY, Y(sign)-stemLength);
                    for(const Chord& chord: beam) for(Sign sign: chord) stemY = min(stemY, Y(sign)-shortStemLength);
                } else {
                    for(const Chord& chord: beam) for(Sign sign: chord) stemY = min(stemY, Y(sign)+stemLength);
                    for(const Chord& chord: beam) for(Sign sign: chord) stemY = max(stemY, Y(sign)+shortStemLength);
                }
                stemY = stemUp ? min(stemY, staffY(sign.staff, -4)) : max(stemY, staffY(sign.staff, -4));
                for(const Chord& chord: beam) for(Sign sign: chord) {
                    int x = X(sign) + dx;
					float y = Y(sign) + dy;
					{vec2 min(x, ::min(y, stemY)), max(x+stemWidth, ::max(stemY, y));
						notation.fills.append(min, max-min); }
                }
				{vec2 min (X(beam.first()[0]) + dx, stemY-beamWidth/2+1), max(X(beam.last ()[0]) + dx + stemWidth, stemY+beamWidth/2);
					notation.fills.append(min, max-min);}
            }

            for(const Chord& chord: beam) {
                Sign sign = stemUp ? chord.first() : chord.last();
                int x = X(sign) + noteSize.x/2;
                int step = clefStep(sign.note.clef.clefSign, sign.note.step);
                int y = Y(sign) + (stemUp?1:-1) * (lineInterval+(step%2?0:halfLineInterval));
				if(chord.first().note.staccato) { glyph(vec2(x,y),"scripts.staccato"_); y+=lineInterval; }
				if(chord.first().note.tenuto) { glyph(vec2(x,y),"scripts.tenuto"_); y+=lineInterval; }
                int y2 = staffY(sign.staff, stemUp ? -10 : 2);
                y = stemUp ? max(y,y2) : min(y,y2);
                y -= (stemUp?0:glyphSize("scripts.sforzato"_).y/2);
				if(chord.first().note.accent) { glyph(vec2(x,y),"scripts.sforzato"_); y+=lineInterval; }
            }
            beam.clear();

            for(const array<Sign>& slur: pendingSlurs[sign.staff]) {
                int sum = 0; for(Sign sign: slur) sum += clefStep(sign.note.clef.clefSign, sign.note.step);
                int slurDown = (int(slur.size) > count ? sum < (int(slur.size) * -4) : stemUp) ? 1 : -1;

                int y = slurDown>0 ? -1000 : 1000;
                for(Sign sign: slur) {
                    y = slurDown>0 ? max(y, Y(sign)) : min(y, Y(sign));
                }
                Sign first = slur.first(); if(slurDown<0) for(Sign sign: slur) if(sign.time==first.time) first=sign; // Top note of chord
                Sign last = slur.last(); if(slurDown>0) for(int i=slur.size-1; i>=0; i--) if(slur[i].time==last.time) last=slur[i]; // Bottom note of chord

                vec2 p0 = vec2(P(first)) + vec2(noteSize.x/2, slurDown*1*noteSize.y);
                vec2 p1 = vec2(P(last)) + vec2(noteSize.x/2, slurDown*2*noteSize.y);
                vec2 k0 = vec2(p0.x, y) + vec2(0, slurDown*2*noteSize.y);
                vec2 k0p = k0 + vec2(0, slurDown*noteSize.y/2);
                vec2 k1 = vec2(p1.x, y) + vec2(0, slurDown*2*noteSize.y);
                vec2 k1p = k1 + vec2(0, slurDown*noteSize.y/2);
				cubics.append( copyRef(ref<vec2>({p0,k0,k1,p1,k1p,k0p})) );
            }
            pendingSlurs[sign.staff].clear();
        }

        uint staff = sign.staff;
        if(timeTrack.contains(sign.time)) x = timeTrack.at(sign.time); // Synchronizes with previously laid signs
        else timeTrack.insert(sign.time, x); // Marks position for future signs

        /**/ if(sign.type == Sign::Note) {
            Note& note = sign.note;
            assert_(note.clef.octave == clefs.at(sign.staff).octave); // FIXME: key relies on correct octave`
            note.clef = clefs.at(sign.staff);
			vec2 p = vec2(x, Y(sign));
            Duration duration = note.duration;
			note.blitIndex = notation.blits.size;
			int dx = glyph(p, "noteheads.s"_+str(min(2,int(duration))), note.grace?graceFont:font);
            int step = clefStep(note.clef.clefSign, note.step);
			for(int s=2; s<=step; s+=2) { int y=staffY(staff, s); notation.fills.append(vec2(x-dx/3,y),vec2(dx*5/3,1)); }
			for(int s=-10; s>=step; s-=2) { int y=staffY(staff, s); notation.fills.append(vec2(x-dx/3,y),vec2(dx*5/3,1)); }
			if(note.slash) parallelograms.append( Parallelogram{p+vec2(-dx+dx/2,dx), p+vec2(dx+dx/2,-dx), 1} );
			if(note.dot) glyph(p+vec2(dx*4/3,0),"dots.dot"_);
            x += 2*dx;

            chord.insertSorted(sign);

			if(duration>=Half) {
				if(beam && beam.last().last().time == sign.time) beam.last().insertSorted(sign);
				else beam.append( copyRef(ref<Sign>({sign})) );
			}

            array<Sign>& slur = slurs[sign.staff];
			if(slur) slur.append( sign );
            if(note.slur) {
				if(!slur) slur.append( sign ); // Starts new slur (only if visible)
				else { pendingSlurs[sign.staff].append( move(slur) ); } // Stops
            }

			if(note.tie == Note::NoTie || note.tie == Note::TieStart) notes.sorted(sign.time).append( note );
        }
        else if(sign.type == Sign::Rest) {
			vec2 p = vec2(x, staffY(staff, -4));
            if(sign.rest.duration == Whole) x+= 3*glyph(p, "rests.0"_);
            else if(sign.rest.duration == Half) x+= 3*glyph(p, "rests.1"_);
            else if(sign.rest.duration == Quarter) x+= 3*glyph(p, "rests.2"_);
            else if(sign.rest.duration == Eighth) x+= 3*glyph(p, "rests.3"_);
            else if(sign.rest.duration == Sixteenth) x+= 3*glyph(p, "rests.4"_);
            else error(int(sign.rest.duration));
        }
        else if(sign.type == Sign::Measure) {
            //if(sign.staff==1 && x > int(target.width) && !slurs[0] && !slurs[1]) break;
            if(sign.staff==0) {
				{vec2 min(x-barWidth+barWidth/2, staffY(0,0)), max(x+barWidth/2, staffY(1,-8));
					notation.fills.append(min, max-min); } // Bar
                // Raster
                for(int staff: range(staffCount)) {
                    for(int line: range(5)) {
                        int y = staffY(staff, -line*2);
						{vec2 min(measures.last(), y), max(x, y+lineWidth);
							notation.fills.append(min, max-min);}
                    }
                }
				measures.append( x );
				measureToChord.append( notes.size() );
                x += noteSize.x;
                timeTrack.at(sign.time).direction = x;
                uint sx = x;
				for(uint8 code: str(sign.measure.index)) {
                    uint16 index = textFont.index(code);
					const Font::Glyph& glyph = textFont.render(index);
					assert_(glyph.image);
					notation.blits.append(vec2(int2(sx, staffY(0, 16))+glyph.offset), vec2(glyph.image.size), share(glyph.image));
					sx += textFont.metrics(index).advance;
                }
            }
        }
        else if(sign.type == Sign::Pedal) {
            int y = staffY(1, -24);
			if(sign.pedal.action == Ped) glyph(vec2(x, y), "pedal.Ped"_);
            if(sign.pedal.action == Start) pedalStart = x + glyphSize("pedal.Ped"_).x;
            if(sign.pedal.action == Change || sign.pedal.action == PedalStop) {
				{vec2 min(pedalStart, y), max(x, y+1);
					notation.fills.append(min, max-min);}
				if(sign.pedal.action == PedalStop) notation.fills.append(vec2(x-1, y-lineInterval), vec2(1, lineInterval));
                else {
					parallelograms.append(vec2(x, y-1), vec2(x+noteSize.x/2, y-noteSize.x), 2.f);
					parallelograms.append(vec2(x+noteSize.x/2, y-noteSize.x), vec2(x+noteSize.x, y), 2.f);
                    pedalStart = x + noteSize.x;
                }
            }
        }
        else if(sign.type == Sign::Wedge) {
            uint x = timeTrack.at(sign.time).direction;
            int y = (staffY(0, -8)+staffY(1, 0))/2;
            if(sign.wedge.action == WedgeStop) {
                bool crescendo = wedgeStart.wedge.action == Crescendo;
				parallelograms.append( vec2(timeTrack.at(wedgeStart.time).direction, y+(-!crescendo-1)*3), vec2(x, y+(-crescendo-1)*3), 1.f);
				parallelograms.append( vec2(timeTrack.at(wedgeStart.time).direction, y+(!crescendo-1)*3), vec2(x, y+(crescendo-1)*3), 1.f);
            } else wedgeStart = sign;
        }
        else if(sign.type == Sign::Dynamic) {
            string word = ref<string>({"ppp"_,"pp"_,"p"_,"mp"_,"mf"_,"f"_,"ff"_,"fff"_})[uint(sign.dynamic.loudness)];
			float w = 0;
			for(char character: word.slice(0,word.size-1)) w += font.metrics(font.index(string{character})).advance;
			w += glyphSize({word.last()}).x;
            int& x = timeTrack.at(sign.time).direction;
			x -= w/2; x += glyphSize({word[0]}).x/2;
            for(char character: word) {
				x += glyph(vec2(x, (staffY(0, -8)+staffY(1, 0))/2), {character});
            }
        } else if(sign.type == Sign::Clef) {
            string change = clefs.contains(sign.staff)?"_change"_:""_;
            Clef clef = sign.clef;
            assert_(!clef.octave);
            if(!clefs.contains(sign.staff) || clefs.at(sign.staff).clefSign != sign.clef.clefSign) {
                clefs[sign.staff] = sign.clef;
                x += noteSize.x;
				if(clef.clefSign==Treble) x += glyph(vec2(x, Y(clefs,staff,4)), "clefs.G"_+change);
				if(clef.clefSign==Bass) x += glyph(vec2(x, Y(clefs,staff,-4)),"clefs.F"_+change);
                x += noteSize.x;
                if(staff==0) x=X(sign);
                timeTrack.at(sign.time).direction = x;
            }
        }
        else if(sign.type==Sign::KeySignature) {
            keySignature = sign.keySignature;
            int fifths = keySignature.fifths;
            for(int i: range(abs(fifths))) {
                int step = (fifths>0?2:4) + ((fifths>0 ? 4 : 3) * i +2)%7;
				glyph(vec2(x, Y(clefs,0,step)), fifths<0?"accidentals.flat"_:"accidentals.sharp"_);
				x += glyph(vec2(x, Y(clefs,1,step - (clefs[1u].clefSign==Bass ? 14 : 0))), fifths<0?"accidentals.flat"_:"accidentals.sharp"_);
            }
            x += noteSize.x;
            timeTrack.at(sign.time).direction = x;
        }
        else if(sign.type==Sign::TimeSignature) {
            timeSignature = sign.timeSignature;
            static constexpr string numbers[10] = {"zero"_,"one"_,"two"_,"three"_,"four"_,"five"_,"six"_,"seven"_,"eight"_,"nine"_};
			glyph(vec2(x, staffY(0, -4)),numbers[timeSignature.beats]);
			glyph(vec2(x, staffY(1, -4)),numbers[timeSignature.beats]);
			glyph(vec2(x, staffY(0, -8)),numbers[timeSignature.beatUnit]);
			x += 2*glyph(vec2(x, staffY(1, -8)),numbers[timeSignature.beatUnit]);
        }
        else if(sign.type == Sign::Metronome) {
			text(vec2(x, staffY(0, 16)), "♩="_+str(sign.metronome.perMinute), textFont);
        }

        if(timeTrack.contains(sign.time+sign.duration)) timeTrack.at(sign.time+sign.duration) = x; // Updates end position for future signs
        else timeTrack.insert(sign.time+sign.duration, x);
    }

    // Vertical center align
	vec2 offset = vec2(0/*-position*/, (height - sizeHint(0).y)/2 + 4*lineInterval);
	for(auto& o: notation.fills) o.origin += offset;
    for(Parallelogram& p: parallelograms) p.min+=offset, p.max+=offset;
	for(auto& o: notation.blits) o.origin += offset;
    for(Cubic& c: cubics) for(vec2& p: c) p+=vec2(offset);
}

buffer<uint> Sheet::synchronize(const ref<uint>& midiNotes) {
	array<uint> midiToBlit (midiNotes.size, 0);
    map<uint, array<Note>> notes = copy(this->notes);
    array<uint> chordExtra;
	array<Glyph> debug;
    uint chordIndex = 0;
    while(chordIndex<notes.size()) {
        if(!notes.values[chordIndex]) {
			chordIndex++; chordToNote.append( midiToBlit.size );
            if(chordIndex==notes.size()) break;
            array<Note>& chord = notes.values[chordIndex];
            chordExtra.filter([&](uint midiIndex){ // Tries to match any previous extra to next notes
                uint midiKey = midiNotes[midiIndex];
                int match = chord.indexOf(midiKey);
                if(match < 0) return false; // Keeps
                Note note = chord.take(match);
                assert_(note.key == midiKey);
                midiToBlit[midiIndex] = note.blitIndex;
                //log_("O"_+str(note.key));
				vec2 p = notation.blits[note.blitIndex].origin;
				text(p+vec2(noteSize.x, 2), "O"_+str(note.key), smallFont, debug);
                orderErrors++;
                return true; // Discards
            });
            if(chordExtra) {
                //log_("+"_+str(apply(chordExtra, [&](const uint index){return midiNotes[index];})));
                assert_(chordExtra.size<=2, chordExtra.size);
                extraErrors+=chordExtra.size;
                chordExtra.clear();
            }
			if(!notes.values[chordIndex]) { chordIndex++; chordToNote.append( midiToBlit.size ); }
            assert_(chordIndex<notes.size());
            if(chordIndex==notes.size()) break;
        }
        assert_(chordIndex<notes.size());
        array<Note>& chord = notes.values[chordIndex];
        assert_(chord);

        uint midiIndex = midiToBlit.size;
        assert_(midiIndex < midiNotes.size);
        uint midiKey = midiNotes[midiIndex];

        if(extraErrors > 9 || wrongErrors > 6 || missingErrors > 9 || orderErrors > 7) {
            log("MID", midiNotes.slice(midiIndex,7));
            log("XML", chord);
            synchronizationFailed = true;
			notation.glyphs.append( move(debug) );
            break;
        }

        int match = chord.indexOf(midiKey);
        if(match >= 0) {
            Note note = chord.take(match);
            assert_(note.key == midiKey);
			midiToBlit.append( note.blitIndex );
			vec2 p = notation.blits[note.blitIndex].origin;
			text(p+vec2(noteSize.x, 2), str(note.key), smallFont, debug);
        } else if(chordExtra && chord.size == chordExtra.size) {
            int match = notes.values[chordIndex+1].indexOf(midiNotes[chordExtra[0]]);
            if(match >= 0) {
                assert_(chord.size<=3, chord);
                //log_("-"_+str(chord));
                missingErrors += chord.size;
                chord.clear();
                chordExtra.filter([&](uint index){
					if(!notes.values[chordIndex]) { chordIndex++; chordToNote.append( midiToBlit.size ); }
                    array<Note>& chord = notes.values[chordIndex];
                    assert_(chord, chordIndex, notes.size());
                    int match = chord.indexOf(midiNotes[index]);
                    if(match<0) return false; // Keeps as extra
                    midiKey = midiNotes[index];
                    Note note = chord.take(match);
                    assert_(midiKey == note.key);
                    midiToBlit[index] = note.blitIndex;
					vec2 p = notation.blits[note.blitIndex].origin;
					text(p+vec2(noteSize.x, 2), str(note.key), smallFont, debug);
                    return true; // Discards extra as matched to next chord
                });
            } else {
                assert_(midiKey != chord[0].key);
                uint previousSize = chord.size;
                chord.filter([&](const Note& note) {
                    if(midiNotes.slice(midiIndex,5).contains(note.key)) return false; // Keeps as extra
                    uint midiIndex = chordExtra.take(0);
                    uint midiKey = midiNotes[midiIndex];
                    assert_(note.key != midiKey);
                    //log_("!"_+str(note.key, midiKey));
					vec2 p = notation.blits[note.blitIndex].origin;
					text(p+vec2(noteSize.x, 2), str(note.key)+"?"_+str(midiKey)+"!"_, smallFont, debug);
                    wrongErrors++;
                    return true; // Discards as wrong
                });
                if(previousSize == chord.size) { // No notes have been filtered out as wrong, remaining are extras
                    assert_(chordExtra && chordExtra.size<=3, chordExtra.size);
                    //log_("+"_+str(apply(chordExtra, [&](const uint index){return midiNotes[index];})));
                    extraErrors += chordExtra.size;
                    chordExtra.clear();
                }
            }
        } else {
			midiToBlit.append( -1 );
			chordExtra.append( midiIndex );
        }
    }
	assert_(chordToNote.size == this->notes.size(), chordToNote.size, this->notes.size());
    log(extraErrors, wrongErrors, missingErrors, orderErrors);
    return move(midiToBlit);
}

Graphics Sheet::graphics(int2 unused size) {
	//Graphics graphics;
	//for(Rect r: notation.fills) fill(target, offset+r);
	//for(Parallelogram p: parallelograms) parallelogram(target, offset+p.min, offset+p.max, p.dy);
	//for(uint i: range(blits.size)) { const Blit& b=blits[i]; blit(target, offset+b.position, b.image, colors.value(i, black)); }
	//for(const Cubic& c: cubics) { buffer<vec2> points(c.size); for(uint i: range(c.size)) points[i]=vec2(offset)+c[i]; cubic(target, points); }*/
	return copy(notation); //FIXME: shared
}

int Sheet::measureIndex(int x0) {
    if(x0<measures[0]) return -1;
    for(uint i: range(measures.size-1)) if(measures[i]<=x0 && x0<measures[i+1]) return i;
    assert_(x0 >= measures.last()); return measures.size;
}

/*bool Sheet::mouseEvent(int2, int2, Event, Button button) {
    int index = measureIndex(position);
    if(button==WheelUp && index>0) { position=measures[index-1]; return true; } //TODO: previous measure
    if(button==WheelDown && index<int(measures.size-1)) { position=measures[index+1]; return true; } //TODO: next measure
    return false;
}*/
