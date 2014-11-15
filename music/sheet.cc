#include "sheet.h"
#include "notation.h"
//#include "utf8.h"
#include "text.h"

static String str(const Sign& o) {
	if(o.type==Sign::Note) return str(o.note.key);
	error(int(o.type));
}

float glyph(vec2 origin, string name, Font& font, array<Glyph>& glyphs) {
	uint index = font.index(name);
	glyphs.append(origin, font, index, index);
	return font.metrics(index).advance;
}

uint text(vec2 origin, string message, Font& /*font*/, array<Glyph>& glyphs) {
	Text text(message, 6.f*Sheet::halfLineInterval, 0, 1, 0, "LinLibertine");
	auto textGlyphs = move(text.graphics(0)->glyphs);
	for(auto& glyph: textGlyphs) { glyph.origin+=origin; glyphs.append(glyph); }
	return text.sizeHint(0).x;
	/*for(uint code: toUCS4(text)) {
		uint index = font.index(code);
		if(code!=' ') glyphs.append(origin, font, index, index);
		origin.x += font.metrics(index).advance;
    }
	return origin.x;*/
}

// Layouts notations to graphic primitives (and parses notes to MIDI keys)
Sheet::Sheet(ref<Sign> signs, uint ticksPerQuarter, ref<uint> midiNotes) {
	map<uint, array<Sign>> notes; // Signs for notes (time, key, blitIndex)
	uint measureIndex=1, pageIndex=1, pageLineIndex=1, lineMeasureIndex=1;
    map<uint, Clef> clefs; KeySignature keySignature={0}; TimeSignature timeSignature={0,0};
    typedef array<Sign> Chord; // Signs belonging to a same chord (same time)
    Chord chords[2]; // Current chord (per staff)
    array<Chord> beams[2]; // Chords belonging to current beam (per staff) (also for correct single chord layout)
	array<array<Sign>> pendingSlurs; // Slurs pending rendering (waiting for beams)
	map<int, array<Sign>> slurs; // Signs belonging to current slur
	float pedalStart = 0; // Last pedal start/change position
	Sign wedgeStart {.wedge={}}; // Current wedge
	Sign octaveStart[2] {{.octave=OctaveStop}, {.octave=OctaveStop}}; // Current octave shift (for each staff)
	struct Position { // Holds current pen position for each line
		float staffs[2];
		float middle; // Dynamic, Wedge
		float metronome; // Metronome
		float octave; // OctaveShift
		float bottom; // Pedal
		/// Maximum position
		float maximum() { return max(max(staffs), max(middle, max(max(metronome, octave), bottom))); }
		/// Synchronizes staff positions to \a x
		void setStaves(float x) { for(float& staffX: staffs) staffX = x; }
		/// Synchronizes all positions to \a x
		void setAll(float x) { setStaves(x); middle = x; metronome = x; octave = x; bottom = x; }
    };
	map<int64, Position> timeTrack; // Maps times to positions

	{//int x = 0;
		/*// System
	vec2 p0 = vec2(x+noteSize.x, staffY(0, 0));
	vec2 p1 = vec2(x+noteSize.x, staffY(1, -8));
	vec2 pM = vec2(x, (p0+p1).y/2);
	vec2 c0[2] = {vec2(pM.x, p0.y), vec2((pM+p0).x/2, p0.y)};
	vec2 c0M[2] = {vec2((pM+p0).x/2, pM.y), vec2(p0.x, pM.y)};
	vec2 c1M[2] = {vec2((pM+p1).x/2, pM.y), vec2(p1.x, pM.y)};
	vec2 c1[2] = {vec2(pM.x, p1.y), vec2((pM+p1).x/2, p1.y)};
	cubics.append( copyRef(ref<vec2>({p0,c0[0],c0M[0],pM,c1M[0],c1[0],p1,c1[1],c1M[1],pM,c0M[1],c0[1]})) );
	x += noteSize.x;*/
		/*{vec2 min(x-1, staffY(0, 0)), max(1, staffY(1, -8));
			notation->fills.append(min, max-min);}*/
		measureBars.insert(/*t*/0u, /*x*/0.f);
		timeTrack.insert(0u, {{0,0},0,0,0,0/*{x,x},x*/});
	}
	Graphics measure;
	for(size_t signIndex: range(signs.size)) {
		Sign sign = signs[signIndex];

		// Meta sign
		if(sign.type == Sign::Slur) {
			/*if(sign.slur.type==SlurStart) {
				assert_(!slurs.contains(int(sign.staff)), int(sign.staff), slurs.keys, measureIndex, sign.slur.index);
				slurs.insert(int(sign.staff)); // Starts new slur
			}
			else {
				pendingSlurs.append(slurs.take(int(sign.staff))); // Stops current slur
			} FIXME*/
			continue;
		}

		auto X = [&](const Sign& sign) -> float& {
			if(!timeTrack.contains(sign.time)) {
				log("!timeTrack.contains(sign.time)", sign.time);
				size_t index = timeTrack.keys.linearSearch(sign.time);
				index = min(index, timeTrack.keys.size-1);
				//assert_(index < timeTrack.keys.size, index, timeTrack.keys.size, sign.time, timeTrack.keys);
				float x = timeTrack.keys[index];
				timeTrack.insert(sign.time, {{x,x},x,x,x,x});
			}
			assert_(timeTrack.contains(sign.time), withName(int(sign.type), sign.note.step, sign.time, sign.duration,
					measureIndex, pageIndex, pageLineIndex, lineMeasureIndex, signIndex));
			if(sign.type == Sign::Metronome) return timeTrack.at(sign.time).metronome;
			if(sign.type == Sign::OctaveShift) return timeTrack.at(sign.time).octave;
			if(sign.type==Sign::Dynamic || sign.type==Sign::Wedge) return timeTrack.at(sign.time).middle;
			if(sign.type == Sign::Pedal) return timeTrack.at(sign.time).bottom;
			assert_(sign.staff < 2, int(sign.type));
			return timeTrack.at(sign.time).staffs[sign.staff];
		};
		auto P = [&](const Sign& sign) { return vec2(X(sign), Y(sign)); };

		for(uint staff: range(staffCount)) {
			// Layout tails and beams
			array<Chord>& beam = beams[staff];
			//log(sign.time, ticksPerQuarter, timeSignature.beats*timeSignature.beatUnit)
			if(beam &&
					((sign.type == Sign::Measure) || // Full beat
					 (sign.time%(ticksPerQuarter*timeSignature.beatUnit/4) == 0 && sign.time>beam[0][0].time) || // Half beat
					 (beam[0][0].time%ticksPerQuarter && sign.time>beam[0][0].time) || // Off beat (stem after complete chord)
					 (sign.staff == staff && sign.type == Sign::Rest) || // Rest
					 (sign.staff == staff && sign.type == Sign::Note
					  && (beam.last().last().note.duration < Eighth || sign.note.duration < Eighth) && sign.time > beam[0][0].time) // Increasing time
					 )) {
				int sum = 0, count=0; for(const Chord& chord: beam) { for(Sign sign: chord) sum += clefStep(sign); count+=chord.size; }
				bool stemUp = sum < -4*count; // sum/count<-4 (Average note height below mid staff)
				int dx = (stemUp ? noteSize.x - 2 : 0);
				int dy = (stemUp ? 0 : 0);

				if(beam.size==1) { // Draws single stem
					assert_(beam[0]);
					Sign sign = stemUp ? beam[0].last() : beam[0].first();
					int x = X(sign) + dx;
					int yMin = Y(beam[0].first());
					int yMax = Y(beam[0].last());
					int yBase = stemUp ? yMin : yMax + dy;
					int yStem = stemUp ? min(yMax-stemLength, staffY(staff, -4)) : max(yMin+stemLength, staffY(staff, -4));
					{vec2 min (x, ::min(yBase, yStem)), max(x+stemWidth, ::max(yBase, yStem));
						measure.fills.append(min, max-min); }
					/**/ if(sign.note.duration==Eighth) glyph(vec2(x+stemWidth, yStem), stemUp?"flags.u3"_:"flags.d3"_, font, measure.glyphs);
					else if(sign.note.duration==Sixteenth) glyph(vec2(x+stemWidth, yStem), stemUp?"flags.u4"_:"flags.d4"_, font, measure.glyphs);
					else if(sign.note.duration==Thirtysecond) glyph(vec2(x+stemWidth, yStem), stemUp?"flags.u5"_:"flags.d5"_, font, measure.glyphs);
					else if(sign.note.duration==Sixtyfourth) glyph(vec2(x+stemWidth, yStem), stemUp?"flags.u6"_:"flags.d6"_, font, measure.glyphs);
				} else if(beam.size==2) { // Draws slanted beam
					float x[2], base[2], tip[2];
					for(uint i: range(2)) {
						const Chord& chord = beam[i];
						Sign sign = chord.first();
						x[i] = X(sign) + dx;
						base[i] = Y(stemUp?chord.first():chord.last()) + dy;
						tip[i] = Y(stemUp?chord.last():chord.first())+(stemUp?-1:1)*(stemLength-noteSize.y/2);
					}
					float farTip = stemUp ? min(tip[0],tip[1]) : max(tip[0],tip[1]);
					float delta[2] = {clip(-lineInterval, tip[0]-farTip, lineInterval), clip(-lineInterval, tip[1]-farTip, lineInterval)};
					farTip = stemUp ? min(farTip, staffY(staff, -4)) : max(farTip, staffY(staff, -4));
					for(uint i: range(2)) {
						vec2 min(x[i], ::min(base[i],farTip+delta[i])), max(x[i]+stemWidth, ::max(base[i],farTip+delta[i]));
						measure.fills.append(min, max-min);
					}
					Sign sign[2] = { stemUp?beam.first().last():beam.first().first(), stemUp?beam.last().last():beam.last().first()};
					vec2 p0 (X(sign[0])+dx, farTip+delta[0]-beamWidth/2);
					vec2 p1 (X(sign[1])+dx+stemWidth, farTip+delta[1]-beamWidth/2);
					measure.parallelograms.append(p0, p1, beamWidth);
				} else { // Draws horizontal beam
					float stemY = stemUp ? -inf : inf;
					if(stemUp) {
						for(const Chord& chord: beam) for(Sign sign: chord) stemY = max(stemY, Y(sign)-stemLength);
						for(const Chord& chord: beam) for(Sign sign: chord) stemY = min(stemY, Y(sign)-shortStemLength);
					} else {
						for(const Chord& chord: beam) for(Sign sign: chord) stemY = min(stemY, Y(sign)+stemLength);
						for(const Chord& chord: beam) for(Sign sign: chord) stemY = max(stemY, Y(sign)+shortStemLength);
					}
					stemY = stemUp ? min(stemY, staffY(staff, -4)) : max(stemY, staffY(staff, -4));
					for(const Chord& chord: beam) for(Sign sign: chord) { // Stems
						int x = X(sign) + dx;
						float y = Y(sign) + dy;
						{vec2 min(x, ::min(y, stemY)), max(x+stemWidth, ::max(stemY, y));
							measure.fills.append(min, max-min); }
					}
					// Beam
					for(size_t chordIndex: range(beam.size-1)) {
						const Chord& chord = beam[chordIndex];
						Duration duration = chord[0].note.duration;
						//for(Sign sign: chord) assert_(sign.note.duration == duration, int(sign.note.duration), int(duration));
						for(size_t index: range(duration-Quarter)) {
							float Y = stemY + (stemUp ? 1 : -1) * float(index) * beamWidth;
							vec2 min (X(chord[0]) + dx, Y-beamWidth/2+1), max(X(beam[chordIndex+1][0]) + dx + stemWidth, Y+beamWidth/2);
							measure.fills.append(min, max-min);
						}
					}
				}

				for(const Chord& chord: beam) {
					Sign sign = stemUp ? chord.first() : chord.last();
					int x = X(sign) + noteSize.x/2;
					int step = clefStep(sign);
					int y = Y(sign) + (stemUp?1:-1) * (lineInterval+(step%2?0:halfLineInterval));
					if(chord.first().note.staccato) { glyph(vec2(x,y),"scripts.staccato"_, font, measure.glyphs); y+=lineInterval; }
					if(chord.first().note.tenuto) { glyph(vec2(x,y),"scripts.tenuto"_, font, measure.glyphs); y+=lineInterval; }
					int y2 = staffY(staff, stemUp ? -10 : 2);
					y = stemUp ? max(y,y2) : min(y,y2);
					y -= (stemUp?0:glyphSize("scripts.sforzato"_).y/2);
					if(chord.first().note.accent) { glyph(vec2(x,y),"scripts.sforzato"_, font, measure.glyphs); y+=lineInterval; }
				}
				beam.clear();

				for(const array<Sign>& slur: pendingSlurs) {
					int sum = 0; for(Sign sign: slur) sum += clefStep(sign);
					int slurDown = (int(slur.size) > count ? sum < (int(slur.size) * -4) : stemUp) ? 1 : -1;

					float y = slurDown>0 ? -inf : inf;
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
					measure.cubics.append(Cubic(copyRef(ref<vec2>({p0,k0,k1,p1,k1p,k0p}))));
				}
				pendingSlurs.clear();
			}

			// Layout accidentals
			Chord& chord = chords[staff];
			if(chord && sign.time != chord.last().time) {
				float lastY = -inf, dx = 0;
				for(Sign sign: chord.reverse()) {
					if(!sign.note.accidental) continue;
					int y = Y(sign);
					if(abs(y-lastY)<=lineInterval) dx -= noteSize.x/2;
					else dx = 0;
					assert_(size_t(sign.note.accidental-1) < 5, int(sign.note.accidental));
					String name = "accidentals."_+ref<string>({"flatflat"_,"flat"_,"natural"_,"sharp"_,"doublesharp"})[sign.note.accidental-1];
					glyph(vec2(X(sign)-glyphSize(name).x+dx, y), name, font, measure.glyphs);
					lastY = y;
				}
				chord.clear();
			}
		}

		uint staff = sign.staff;
		if(staff < 2) { // Staff signs
			float x = X(sign);

			/**/ if(sign.type == Sign::OctaveShift) {
				/**/  if(sign.octave == Down) {
					x += text(vec2(x, staffY(0, 8)), "8"+superscript("va"), textFont, measure.glyphs);
					clefs[staff].octave++;
					timeTrack.at(sign.time).octave = x;
				}
				else if(sign.octave == OctaveStop) {
					assert_(octaveStart[staff].octave==Down, int(octaveStart[staff].octave));
					clefs[staff].octave--;
					float start = X(octaveStart[staff]) + noteSize.x;
					float end = x;
					for(float x=start; x<end-noteSize.x; x+=noteSize.x*2) { // FIXME: breaks at each measure
						measure.fills.append(vec2(x,staffY(0, 6)),vec2(noteSize.x,1));
					}
				}
				else error(int(sign.octave));
				octaveStart[staff] = sign;
			}
			else {
				if(sign.type == Sign::Note) {
					Note& note = sign.note;
					//assert_(note.clef.octave == clefs.at(staff).octave); // FIXME: key relies on correct octave
					note.clef = clefs.at(staff);
					vec2 p = P(sign);
					Duration duration = note.duration;
					note.measureIndex = measures.size();
					note.glyphIndex = measure.glyphs.size;
					int dx = glyph(p, "noteheads.s"_+str(min(2,int(duration))), note.grace?graceFont:font, measure.glyphs);
					int step = clefStep(sign);
					for(int s=2; s<=step; s+=2) { int y=staffY(staff, s); measure.fills.append(vec2(x-dx/3,y),vec2(dx*5/3,1)); }
					for(int s=-10; s>=step; s-=2) { int y=staffY(staff, s); measure.fills.append(vec2(x-dx/3,y),vec2(dx*5/3,1)); }
					if(note.slash) measure.parallelograms.append( Parallelogram(p+vec2(-dx+dx/2,dx), p+vec2(dx+dx/2,-dx), 1) );
					if(note.dot) glyph(p+vec2(dx*4/3,0),"dots.dot"_, font, measure.glyphs);
					x += 2*dx;

					if(!note.grace) {
						chords[staff].insertSorted(sign);

						if(duration >= Half) {
							array<Chord>& beam = beams[staff];
							if(beam && beam.last().last().time == sign.time) beam.last().insertSorted(sign);
							else beam.append( copyRef(ref<Sign>({sign})) );
						}

						if(slurs.contains(int(staff))) slurs.at(int(staff)).append( sign );
						if(slurs.contains(-1)) slurs.at(-1).append( sign );
					}
					if(note.tie == Note::NoTie || note.tie == Note::TieStart) notes.sorted(sign.time).append( sign );
				}
				else if(sign.type == Sign::Rest) {
					vec2 p = vec2(x, staffY(staff, -4));
					if(sign.rest.duration == Whole) x+= 3*glyph(p, "rests.0"_, font, measure.glyphs);
					else if(sign.rest.duration == Half) x+= 3*glyph(p, "rests.1"_, font, measure.glyphs);
					else if(sign.rest.duration == Quarter) x+= 3*glyph(p, "rests.2"_, font, measure.glyphs);
					else if(sign.rest.duration == Eighth) x+= 3*glyph(p, "rests.3"_, font, measure.glyphs);
					else if(sign.rest.duration == Sixteenth) x+= 3*glyph(p, "rests.4"_, font, measure.glyphs);
					else if(sign.rest.duration == Thirtysecond) x+= 3*glyph(p, "rests.5"_, font, measure.glyphs);
					else if(sign.rest.duration == Sixtyfourth) x+= 3*glyph(p, "rests.6"_, font, measure.glyphs);
					else error(int(sign.rest.duration));
				}
				else if(sign.type == Sign::Clef) {
					string change = clefs.contains(staff)?"_change"_:""_;
					Clef clef = sign.clef;
					assert_(!clef.octave);
					if((!clefs.contains(staff) || clefs.at(staff).clefSign != sign.clef.clefSign) && !(signIndex>=signs.size-2)) {
						clefs[staff] = sign.clef;
						x += noteSize.x;
						if(clef.clefSign==Treble) x += glyph(vec2(x, staffY(staff, -6)), "clefs.G"_+change, font, measure.glyphs);
						if(clef.clefSign==Bass) x += glyph(vec2(x, staffY(staff, -2)),"clefs.F"_+change, font, measure.glyphs);
						x += noteSize.x;
					}
				}
				else error(int(sign.type));

				if(sign.duration) { // Updates end position for future signs
					if(timeTrack.contains(sign.time+sign.duration)) timeTrack.at(sign.time+sign.duration).setStaves(x);
					else timeTrack.insert(sign.time+sign.duration, {{x,x},x,x,x,x});
				} else timeTrack.at(sign.time).staffs[staff] = x;
			}
		} else {
			assert_(staff == uint(-1));
			assert_(sign.duration == 0);
			if(sign.type == Sign::Measure || sign.type==Sign::KeySignature || sign.type==Sign::TimeSignature) { // Clearing signs (across staves)
				float x = timeTrack.at(sign.time).maximum();
				if(sign.type==Sign::TimeSignature) {
					timeSignature = sign.timeSignature;
					String beats = str(timeSignature.beats);
					String beatUnit = str(timeSignature.beatUnit);
					static constexpr string numbers[10] = {"zero"_,"one"_,"two"_,"three"_,"four"_,"five"_,"six"_,"seven"_,"eight"_,"nine"_};
					float w = glyphSize(numbers[0]).x;
					float W = max(beats.size, beatUnit.size)*w;
					float startX = x;
					x = startX + (W-beats.size*w)/2;
					for(char digit: beats) {
						glyph(vec2(x, staffY(0, -4)),numbers[digit-'0'], font, measure.glyphs);
						x += glyph(vec2(x, staffY(1, -4)),numbers[digit-'0'], font, measure.glyphs);
					}
					float maxX = x;
					x = startX + (W-beatUnit.size*w)/2;
					for(char digit: beatUnit) {
						glyph(vec2(x, staffY(0, -8)),numbers[digit-'0'], font, measure.glyphs);
						x += glyph(vec2(x, staffY(1, -8)),numbers[digit-'0'], font, measure.glyphs);
					}
					maxX = startX+W+w;
					timeTrack.at(sign.time).setStaves(maxX); // Does not clear directions lines
				} else { // Clears all lines (including direction lines)
					if(sign.type == Sign::Measure) {
						measureIndex = sign.measure.measure;
						pageIndex = sign.measure.page;
						pageLineIndex = sign.measure.pageLine;
						lineMeasureIndex = sign.measure.lineMeasure;
						{vec2 min(x-barWidth+barWidth/2, staffY(0,0)), max(x+barWidth/2, staffY(1,-8));
							measure.fills.append(min, max-min); } // Bar
						// Raster
						for(int staff: range(staffCount)) {
							for(int line: range(5)) {
								int y = staffY(staff, -line*2);
								{vec2 min(measureBars.values.last(), y), max(x, y+lineWidth);
									measure.fills.append(min, max-min);}
							}
						}
						measures.insertMulti(Rect(int2(measureBars.values.last()-barWidth+barWidth/2, 0), int2(x+barWidth/2, 0)),
											 shared<Graphics>(move(measure)) );
						measureBars.insert(sign.time, x);
						x += noteSize.x;
						text(vec2(x, staffY(0, 12)), str(pageIndex)+','+str(pageLineIndex)+','+str(lineMeasureIndex)+' '+str(measureIndex), textFont, debug->glyphs);
					}
					else if(sign.type==Sign::KeySignature) {
						keySignature = sign.keySignature;
						int fifths = keySignature.fifths;
						for(int i: range(abs(fifths))) {
							int step = fifths<0 ? 4+(3*i+2)%7 : 6+(4*i+4)%7;
							string symbol = fifths<0?"accidentals.flat"_:"accidentals.sharp"_;
									 glyph(vec2(x, Y(0, clefs[0u].clefSign, step - (clefs[0u].clefSign==Bass ? 14 : 0))), symbol, font, measure.glyphs);
							x += glyph(vec2(x, Y(1, clefs[1u].clefSign, step - (clefs[1u].clefSign==Bass ? 14 : 0))), symbol, font, measure.glyphs);
						}
						x += noteSize.x;
					}
					else error(int(sign.type));
					timeTrack.at(sign.time).setAll(x);
				}
			} else { // Directions signs
				float& x = X(sign);
				if(sign.type == Sign::Metronome) {
					x += text(vec2(x, staffY(0, 12)), "♩="_+str(sign.metronome.perMinute)+" "_, textFont, measure.glyphs);
					if(ticksPerMinutes) log(ticksPerMinutes, "->", int64(sign.metronome.perMinute*ticksPerQuarter)); // FIXME: variable tempo
					ticksPerMinutes = max(ticksPerMinutes, int64(sign.metronome.perMinute*ticksPerQuarter));
				}
				else if(sign.type == Sign::Dynamic) {
					string word = sign.dynamic;
					float w = 0;
					for(char character: word.slice(0,word.size-1)) w += font.metrics(font.index(string{character})).advance;
					w += glyphSize({word.last()}).x;
					x -= w/2; x += glyphSize({word[0]}).x/2;
					for(char character: word) {
						x += glyph(vec2(x, (staffY(0, -8)+staffY(1, 0))/2), {character}, font, measure.glyphs);
					}
				} else if(sign.type == Sign::Wedge) {
					int y = (staffY(0, -8)+staffY(1, 0))/2;
					if(sign.wedge == WedgeStop) {
						bool crescendo = wedgeStart.wedge == Crescendo;
						measure.parallelograms.append( vec2(X(wedgeStart), y+(-!crescendo-1)*3), vec2(x, y+(-crescendo-1)*3), 1.f);
						measure.parallelograms.append( vec2(X(wedgeStart), y+(!crescendo-1)*3), vec2(x, y+(crescendo-1)*3), 1.f);
					} else wedgeStart = sign;
				} else if(sign.type == Sign::Pedal) {
					int y = staffY(1, -20);
					if(sign.pedal == Ped) glyph(vec2(x, y), "pedal.Ped"_, font, measure.glyphs);
					if(sign.pedal == Start) pedalStart = x + glyphSize("pedal.Ped"_).x;
					if(sign.pedal == Change || sign.pedal == PedalStop) {
						{vec2 min(pedalStart, y), max(x, y+1);
							measure.fills.append(min, max-min);}
						if(sign.pedal == PedalStop) measure.fills.append(vec2(x-1, y-lineInterval), vec2(1, lineInterval));
						else {
							measure.parallelograms.append(vec2(x, y-1), vec2(x+noteSize.x/2, y-noteSize.x), 2.f);
							measure.parallelograms.append(vec2(x+noteSize.x/2, y-noteSize.x), vec2(x+noteSize.x, y), 2.f);
							pedalStart = x + noteSize.x;
						}
					}
				} else error(int(sign.type));
			}
		}
	}
	if(!ticksPerMinutes) ticksPerMinutes = 120*ticksPerQuarter; // TODO: default tempo from audio

	midiToSign = buffer<Sign>(midiNotes.size, 0);
	array<uint> chordExtra;

	constexpr bool logErrors = true;
	while(chordToNote.size < notes.size()) {
		if(!notes.values[chordToNote.size]) {
			chordToNote.append( midiToSign.size );
			if(chordToNote.size==notes.size()) break;
			array<Sign>& chord = notes.values[chordToNote.size];
			chordExtra.filter([&](uint midiIndex){ // Tries to match any previous extra to next notes
				uint midiKey = midiNotes[midiIndex];
				int match = chord.indexOf(midiKey);
				if(match < 0) return false; // Keeps
				Sign sign = chord.take(match); Note note = sign.note;
				assert_(note.key == midiKey);
				midiToSign[midiIndex] = sign;
				if(logErrors) log("O",str(note.key));
				vec2 p = measures.values[note.measureIndex]->glyphs[note.glyphIndex].origin;
				text(p+vec2(noteSize.x, 2), "O"_+str(note.key), smallFont, debug->glyphs);
				orderErrors++;
				return true; // Discards
			});
			if(chordExtra) {
				if(logErrors) log("+"_+str(apply(chordExtra, [&](const uint index){return midiNotes[index];})));
				if(!(chordExtra.size<=2)) { log("chordExtra.size<=2"); break; }
				assert_(chordExtra.size<=2, chordExtra.size);
				extraErrors+=chordExtra.size;
				chordExtra.clear();
			}
			if(!notes.values[chordToNote.size]) chordToNote.append( midiToSign.size );
			assert_(chordToNote.size<notes.size());
			if(chordToNote.size==notes.size()) break;
		}
		assert_(chordToNote.size<notes.size());
		array<Sign>& chord = notes.values[chordToNote.size];
		assert_(chord);

		uint midiIndex = midiToSign.size;
		assert_(midiIndex < midiNotes.size);
		uint midiKey = midiNotes[midiIndex];

		if(extraErrors > 40 /*FIXME: tremolo*/ || wrongErrors > 9 || missingErrors > 13 || orderErrors > 8) {
		//if(extraErrors || wrongErrors || missingErrors || orderErrors) {
			log(midiIndex, midiNotes.size);
			log("MID", midiNotes.slice(midiIndex,7));
			log("XML", chord);
			break;
		}

		int match = chord.indexOf(midiKey);
		if(match >= 0) {
			Sign sign = chord.take(match); Note note = sign.note;
			assert_(note.key == midiKey);
			midiToSign.append( sign );
			vec2 p = measures.values[note.measureIndex]->glyphs[note.glyphIndex].origin;
			text(p+vec2(noteSize.x, 2), str(note.key), smallFont, debug->glyphs);
		} else if(chordExtra && chord.size == chordExtra.size) {
			int match = notes.values[chordToNote.size+1].indexOf(midiNotes[chordExtra[0]]);
			if(match >= 0) {
				assert_(chord.size<=3/*, chord*/);
				if(logErrors) log("-"_+str(chord));
				missingErrors += chord.size;
				chord.clear();
				chordExtra.filter([&](uint index){
					if(!notes.values[chordToNote.size]) chordToNote.append( midiToSign.size );
					array<Sign>& chord = notes.values[chordToNote.size];
					assert_(chord, chordToNote.size, notes.size());
					int match = chord.indexOf(midiNotes[index]);
					if(match<0) return false; // Keeps as extra
					midiKey = midiNotes[index];
					Sign sign = chord.take(match); Note note = sign.note;
					assert_(midiKey == note.key);
					midiToSign[index] = sign;
					vec2 p = measures.values[note.measureIndex]->glyphs[note.glyphIndex].origin;
					text(p+vec2(noteSize.x, 2), str(note.key), smallFont, debug->glyphs);
					return true; // Discards extra as matched to next chord
				});
			} else {
				assert_(midiKey != chord[0].note.key);
				uint previousSize = chord.size;
				chord.filter([&](const Sign& sign) { Note note = sign.note;
					if(midiNotes.slice(midiIndex,5).contains(note.key)) return false; // Keeps as extra
					uint midiIndex = chordExtra.take(0);
					uint midiKey = midiNotes[midiIndex];
					assert_(note.key != midiKey);
					if(logErrors) log("!"_+str(note.key, midiKey));
					vec2 p = measures.values[note.measureIndex]->glyphs[note.glyphIndex].origin;
					text(p+vec2(noteSize.x, 2), str(note.key)+"?"_+str(midiKey)+"!"_, smallFont, debug->glyphs);
					wrongErrors++;
					return true; // Discards as wrong
				});
				if(previousSize == chord.size) { // No notes have been filtered out as wrong, remaining are extras
					assert_(chordExtra && chordExtra.size<=3, chordExtra.size);
					if(logErrors) log("+"_+str(apply(chordExtra, [&](const uint index){return midiNotes[index];})));
					extraErrors += chordExtra.size;
					chordExtra.clear();
				}
			}
		} else {
			midiToSign.append({.note={}});
			chordExtra.append( midiIndex );
		}
	}
	if(chordToNote.size == notes.size()) assert_(midiToSign.size == midiNotes.size, midiToSign.size, midiNotes.size);
	else {
		firstSynchronizationFailureChordIndex = chordToNote.size;
	}
	if(logErrors && (extraErrors||wrongErrors||missingErrors||orderErrors)) log(extraErrors, wrongErrors, missingErrors, orderErrors);

	auto verticalAlign = [&](Graphics& measure) {
		vec2 offset = vec2(0, -staffY(0,14)+textFont.size);
		for(auto& o: measure.fills) o.origin += offset;
		assert_(!measure.blits);
		for(auto& o: measure.glyphs) o.origin += offset;
		for(auto& o: measure.parallelograms) o.min+=offset, o.max+=offset;
		assert_(!measure.lines);
		for(auto& o: measure.cubics) for(vec2& p: o.points) p+=vec2(offset);
	};
	for(Graphics& measure: measures.values) verticalAlign(measure);
	verticalAlign(debug);
}

inline bool operator ==(const Sign& sign, const uint& key) {
	assert_(sign.type == Sign::Note);
	return sign.note.key == key;
}

shared<Graphics> Sheet::graphics(int2 size, Rect clip) {
	shared<Graphics> graphics;
	size_t first = 0;
	for(; first < measures.size(); first++) if(measures.keys[first].max.x > clip.min.x) break;
	size_t last = first;
	for(; last < measures.size(); last++) if(measures.keys[last].min.x > clip.max.x) break;
	assert_(last > first);
	for(const auto& measure: measures.values.slice(first, last-first)) graphics->graphics.insertMulti(vec2(0), share(measure));
	graphics->offset.y = (size.y - abs(sizeHint(size).y))/2;
	if(firstSynchronizationFailureChordIndex != invalid) graphics->graphics.insertMulti(vec2(0), share(debug));
	return graphics;
}

size_t Sheet::measureIndex(float x) {
	if(x < measureBars.values[0]) return invalid;
	for(size_t i: range(measureBars.size()-1)) if(measureBars.values[i]<=x && x<measureBars.values[i+1]) return i;
	assert_(x >= measureBars.values.last()); return measureBars.size();
}

int Sheet::stop(int unused axis, int currentPosition, int direction=0) {
	int currentIndex = measureIndex(currentPosition);
	return measureBars.values[clip(0, currentIndex+direction, int(measureBars.size()-1))];
}
