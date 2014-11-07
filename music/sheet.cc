#include "sheet.h"
#include "notation.h"
#include "utf8.h"

float Sheet::glyph(vec2 origin, string name, Font& font) {
	uint index = font.index(name);
	notation->glyphs.append(origin, font, index, index);
	return font.metrics(index).advance;
}

uint Sheet::text(vec2 origin, string text, Font& font, array<Glyph>& glyphs) {
    for(uint code: toUCS4(text)) {
		uint index = font.index(code);
		if(code!=' ') glyphs.append(origin, font, index, index);
		origin.x += font.metrics(index).advance;
    }
	return origin.x;
}

// Layouts notations to graphic primitives (and parses notes to MIDI keys)
Sheet::Sheet(ref<Sign> signs, uint divisions, ref<uint> midiNotes) { // Time steps per measure
	uint measureIndex=1, pageIndex=1, pageLineIndex=1, lineMeasureIndex=1;
    map<uint, Clef> clefs; KeySignature keySignature={0}; TimeSignature timeSignature={0,0};
    typedef array<Sign> Chord; // Signs belonging to a same chord (same time)
    Chord chords[2]; // Current chord (per staff)
    array<Chord> beams[2]; // Chords belonging to current beam (per staff) (also for correct single chord layout)
	array<array<Sign>> pendingSlurs[3];
	array<Sign> slurs[3]; // Signs belonging to current slur (pending slurs per staff + across staff)
	float pedalStart = 0; // Last pedal start/change position
    Sign wedgeStart; // Current wedge
	struct Position { // Holds current pen position for each line
		float staffs[2];
		float middle; // Dynamic, Wedge
		float top; // Metronome
		float bottom; // Pedal
		/// Maximum position
		float maximum() { return max(max(staffs), max(middle, max(top, bottom))); }
		/// Synchronizes staff positions to \a x
		void setStaves(float x) { for(float& staffX: staffs) staffX = x; }
		/// Synchronizes all positions to \a x
		void setAll(float x) { setStaves(x); middle = x; top = x; bottom = x; }
    };
	map<uint64, Position> timeTrack; // Maps times to positions
	map<uint, array<Sign>> notes; // Signs for notes (time, key, blitIndex)
	array<Glyph> debug;
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
		{vec2 min(/*x*/-1, staffY(0, 0)), max(1, staffY(1, -8));
			notation->fills.append(min, max-min);}
		measures.append( /*x*/0 );
		measureToChord.append( 0 );
		timeTrack.insert(0u, {{0,0},0,0,0/*{x,x},x*/});
	}
	for(size_t signIndex: range(signs.size)) {
		Sign sign = signs[signIndex];
		auto X = [&](const Sign& sign) -> float& {
			assert_(timeTrack.contains(sign.time), withName(int(sign.type), sign.note.step, sign.time, sign.duration,
					measureIndex, pageIndex, pageLineIndex, lineMeasureIndex, signIndex));
			if(sign.type == Sign::Metronome) return timeTrack.at(sign.time).top;
			if(sign.type==Sign::Dynamic || sign.type==Sign::Wedge) return timeTrack.at(sign.time).middle;
			if(sign.type == Sign::Pedal) return timeTrack.at(sign.time).bottom;
			assert_(sign.staff < 2, int(sign.type));
			return timeTrack.at(sign.time).staffs[sign.staff];
		};
		auto P = [&](const Sign& sign) { return vec2(X(sign),Y(sign)); };

		for(uint staff: range(staffCount)) {
			// Layout tails and beams
			array<Chord>& beam = beams[staff];
			if(beam &&
					((sign.type == Sign::Measure) || // Full beat
					 (sign.time%(timeSignature.beats*divisions) == (timeSignature.beats*divisions)/2 && sign.time>beam[0][0].time) || // Half beat
					 (beam[0][0].time%divisions && sign.time>beam[0][0].time) || // Off beat (stem after complete chord)
					 (sign.staff == staff && sign.type == Sign::Rest) || // Rest
					 (sign.staff == staff && sign.type == Sign::Note
					  && (beam.last().last().note.duration < Eighth || sign.note.duration < Eighth) && sign.time > beam[0][0].time) // Increasing time
					 )) {
				int sum = 0, count=0; for(const Chord& chord: beam) { for(Sign sign: chord) sum += clefStep(sign.note.clef.clefSign, sign.note.step); count+=chord.size; }
				bool stemUp = sum < -4*count; // sum/count<-4 (Average note height below mid staff)
				int dx = (stemUp ? noteSize.x - 2 : 0);
				int dy = (stemUp ? 0 : 0);

				if(beam.size==1) { // Draws single stem
					Sign sign = stemUp ? beam[0].last() : beam[0].first();
					int x = X(sign) + dx;
					int yMin = Y(sign, beam[0].first().note.step);
					int yMax = Y(sign, beam[0].last().note.step);
					int yBase = stemUp ? yMin : yMax + dy;
					int yStem = stemUp ? min(yMax-stemLength, staffY(staff, -4)) : max(yMin+stemLength, staffY(staff, -4));
					{vec2 min (x, ::min(yBase, yStem)), max(x+stemWidth, ::max(yBase, yStem));
						notation->fills.append(min, max-min); }
					/**/ if(sign.note.duration==Eighth) glyph(vec2(x+stemWidth, yStem), stemUp?"flags.u3"_:"flags.d3"_);
					else if(sign.note.duration==Sixteenth) glyph(vec2(x+stemWidth, yStem), stemUp?"flags.u4"_:"flags.d4"_);
				} else if(beam.size==2) { // Draws slanted beam
					float x[2], base[2], tip[2];
					for(uint i: range(2)) {
						const Chord& chord = beam[i];
						Sign sign = chord.first();
						x[i] = X(sign) + dx;
						base[i] = Y(sign, (stemUp?chord.first():chord.last()).note.step) + dy;
						tip[i] = Y(sign, (stemUp?chord.last():chord.first()).note.step)+(stemUp?-1:1)*(stemLength-noteSize.y/2);
					}
					float farTip = stemUp ? min(tip[0],tip[1]) : max(tip[0],tip[1]);
					float delta[2] = {clip(-lineInterval, tip[0]-farTip, lineInterval), clip(-lineInterval, tip[1]-farTip, lineInterval)};
					farTip = stemUp ? min(farTip, staffY(staff, -4)) : max(farTip, staffY(staff, -4));
					for(uint i: range(2)) {
						vec2 min(x[i], ::min(base[i],farTip+delta[i])), max(x[i]+stemWidth, ::max(base[i],farTip+delta[i]));
						notation->fills.append(min, max-min);
					}
					Sign sign[2] = { stemUp?beam.first().last():beam.first().first(), stemUp?beam.last().last():beam.last().first()};
					vec2 p0 (X(sign[0])+dx, farTip+delta[0]-beamWidth/2);
					vec2 p1 (X(sign[1])+dx+stemWidth, farTip+delta[1]-beamWidth/2);
					notation->parallelograms.append(p0, p1, beamWidth);
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
					for(const Chord& chord: beam) for(Sign sign: chord) {
						int x = X(sign) + dx;
						float y = Y(sign) + dy;
						{vec2 min(x, ::min(y, stemY)), max(x+stemWidth, ::max(stemY, y));
							notation->fills.append(min, max-min); }
					}
					{vec2 min (X(beam.first()[0]) + dx, stemY-beamWidth/2+1), max(X(beam.last ()[0]) + dx + stemWidth, stemY+beamWidth/2);
						notation->fills.append(min, max-min);}
				}

				for(const Chord& chord: beam) {
					Sign sign = stemUp ? chord.first() : chord.last();
					int x = X(sign) + noteSize.x/2;
					int step = clefStep(sign.note.clef.clefSign, sign.note.step);
					int y = Y(sign) + (stemUp?1:-1) * (lineInterval+(step%2?0:halfLineInterval));
					if(chord.first().note.staccato) { glyph(vec2(x,y),"scripts.staccato"_); y+=lineInterval; }
					if(chord.first().note.tenuto) { glyph(vec2(x,y),"scripts.tenuto"_); y+=lineInterval; }
					int y2 = staffY(staff, stemUp ? -10 : 2);
					y = stemUp ? max(y,y2) : min(y,y2);
					y -= (stemUp?0:glyphSize("scripts.sforzato"_).y/2);
					if(chord.first().note.accent) { glyph(vec2(x,y),"scripts.sforzato"_); y+=lineInterval; }
				}
				beam.clear();

				for(const array<Sign>& slur: pendingSlurs[staff]) {
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
					notation->cubics.append( copyRef(ref<vec2>({p0,k0,k1,p1,k1p,k0p})) );
				}
				pendingSlurs[staff].clear();
			}

			// Layout accidentals
			Chord& chord = chords[staff];
			if(chord && sign.time != chord.last().time) {
				int lastY = -1000, dx = 0;
				for(Sign sign: chord.reverse()) {
					if(!sign.note.accidental) continue;
					int y = Y(sign);
					if(abs(y-lastY)<=lineInterval) dx -= noteSize.x/2;
					else dx = 0;
					assert_(size_t(sign.note.accidental-1) < 5, int(sign.note.accidental));
					String name = "accidentals."_+ref<string>({"flatflat"_,"flat"_,"natural"_,"sharp"_,"doublesharp"})[sign.note.accidental-1];
					glyph(vec2(X(sign)-glyphSize(name).x+dx, y), name);
					lastY = y;
				}
				chord.clear();
			}
		}

		uint staff = sign.staff;
		if(staff < 2) { // Staff signs
			float x = X(sign);
			//if(timeTrack.contains(sign.time)) x = timeTrack.at(sign.time); // Synchronizes with previously laid signs
			//else timeTrack.insert(sign.time, x); // Marks position for future signs*/

			/**/ if(sign.type == Sign::Note) {
				Note& note = sign.note;
				assert_(note.clef.octave == clefs.at(staff).octave); // FIXME: key relies on correct octave`
				note.clef = clefs.at(staff);
				vec2 p = P(sign);
				Duration duration = note.duration;
				note.glyphIndex = notation->glyphs.size;
				int dx = glyph(p, "noteheads.s"_+str(min(2,int(duration))), note.grace?graceFont:font);
				int step = clefStep(note.clef.clefSign, note.step);
				for(int s=2; s<=step; s+=2) { int y=staffY(staff, s); notation->fills.append(vec2(x-dx/3,y),vec2(dx*5/3,1)); }
				for(int s=-10; s>=step; s-=2) { int y=staffY(staff, s); notation->fills.append(vec2(x-dx/3,y),vec2(dx*5/3,1)); }
				if(note.slash) notation->parallelograms.append( Parallelogram{p+vec2(-dx+dx/2,dx), p+vec2(dx+dx/2,-dx), 1} );
				if(note.dot) glyph(p+vec2(dx*4/3,0),"dots.dot"_);
				x += 2*dx;

				if(!note.grace) {
					chords[staff].insertSorted(sign);

					if(duration >= Half) {
						array<Chord>& beam = beams[staff];
						if(beam && beam.last().last().time == sign.time) beam.last().insertSorted(sign);
						else beam.append( copyRef(ref<Sign>({sign})) );
					}

					array<Sign>& slur = slurs[staff];
					if(slur) slur.append( sign );
					if(note.slur) {
						if(!slur) slur.append( sign ); // Starts new slur (only if visible)
						else { pendingSlurs[staff].append( move(slur) ); } // Stops
					}
				}
				if(note.tie == Note::NoTie || note.tie == Note::TieStart) notes.sorted(sign.time).append( sign );
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
			else if(sign.type == Sign::Clef) {
				string change = clefs.contains(staff)?"_change"_:""_;
				Clef clef = sign.clef;
				assert_(!clef.octave);
				if((!clefs.contains(staff) || clefs.at(staff).clefSign != sign.clef.clefSign) && !(signIndex>=signs.size-2)) {
					clefs[staff] = sign.clef;
					x += noteSize.x;
					if(clef.clefSign==Treble) x += glyph(vec2(x, Y(sign, 4)), "clefs.G"_+change);
					if(clef.clefSign==Bass) x += glyph(vec2(x, Y(sign, -4)),"clefs.F"_+change);
					x += noteSize.x;
					//timeTrack.at(sign.time).note = x;
					//if(staff==0) x=X(sign);
					//timeTrack.at(sign.time).direction = x;
				}
			}
			else error(int(sign.type));

			// Updates end position for future signs
			if(timeTrack.contains(sign.time+sign.duration)) timeTrack.at(sign.time+sign.duration).setStaves(x);
			else timeTrack.insert(sign.time+sign.duration, {{x,x},x,x,x});
		} else {
			assert_(staff == uint(-1));
			assert_(sign.duration == 0);
			if(sign.type == Sign::Measure || sign.type==Sign::KeySignature || sign.type==Sign::TimeSignature) { // Clearing signs (across staves)
				float x = timeTrack.at(sign.time).maximum();
				if(sign.type==Sign::TimeSignature) {
					timeSignature = sign.timeSignature;
					static constexpr string numbers[10] = {"zero"_,"one"_,"two"_,"three"_,"four"_,"five"_,"six"_,"seven"_,"eight"_,"nine"_};
					glyph(vec2(x, staffY(0, -4)),numbers[timeSignature.beats]);
					glyph(vec2(x, staffY(1, -4)),numbers[timeSignature.beats]);
					glyph(vec2(x, staffY(0, -8)),numbers[timeSignature.beatUnit]);
					x += 2*glyph(vec2(x, staffY(1, -8)),numbers[timeSignature.beatUnit]);
					timeTrack.at(sign.time).setStaves(x); // Does not clear directions lines
				} else { // Clears all lines (including direction lines)
					if(sign.type == Sign::Measure) {
						measureIndex = sign.measure.measure;
						pageIndex = sign.measure.page;
						pageLineIndex = sign.measure.pageLine;
						lineMeasureIndex = sign.measure.lineMeasure;
						{vec2 min(x-barWidth+barWidth/2, staffY(0,0)), max(x+barWidth/2, staffY(1,-8));
							notation->fills.append(min, max-min); } // Bar
						// Raster
						for(int staff: range(staffCount)) {
							for(int line: range(5)) {
								int y = staffY(staff, -line*2);
								{vec2 min(measures.last(), y), max(x, y+lineWidth);
									notation->fills.append(min, max-min);}
							}
						}
						measures.append( x );
						measureToChord.append( notes.size() );
						x += noteSize.x;
						text(vec2(x, staffY(0, 16)), str(pageIndex)+','+str(pageLineIndex)+','+str(lineMeasureIndex)+' '+str(measureIndex), textFont, debug);
					}
					else if(sign.type==Sign::KeySignature) {
						keySignature = sign.keySignature;
						int fifths = keySignature.fifths;
						for(int i: range(abs(fifths))) {
							int step = (fifths>0?2:4) + ((fifths>0 ? 4 : 3) * i +2)%7;
							string symbol = fifths<0?"accidentals.flat"_:"accidentals.sharp"_;
									 glyph(vec2(x, Y(0, {clefs[0u].clefSign, 0}, step - (clefs[0u].clefSign==Bass ? 14 : 0))), symbol);
							x += glyph(vec2(x, Y(1, {clefs[1u].clefSign, 0}, step - (clefs[1u].clefSign==Bass ? 14 : 0))), symbol);
						}
						x += noteSize.x;
					}
					else error(int(sign.type));
					timeTrack.at(sign.time).setAll(x);
				}
			} else { // Directions signs
				float& x = X(sign);
				if(sign.type == Sign::Metronome) {
					text(vec2(x, staffY(0, 16)), "♩="_+str(sign.metronome.perMinute), textFont);
				}
				else if(sign.type == Sign::Dynamic) {
					string word = ref<string>({"ppp"_,"pp"_,"p"_,"mp"_,"mf"_,"f"_,"ff"_,"fff"_})[uint(sign.dynamic.loudness)];
					float w = 0;
					for(char character: word.slice(0,word.size-1)) w += font.metrics(font.index(string{character})).advance;
					w += glyphSize({word.last()}).x;
					x -= w/2; x += glyphSize({word[0]}).x/2;
					for(char character: word) {
						x += glyph(vec2(x, (staffY(0, -8)+staffY(1, 0))/2), {character});
					}
				} else if(sign.type == Sign::Wedge) {
					int y = (staffY(0, -8)+staffY(1, 0))/2;
					if(sign.wedge.action == WedgeStop) {
						bool crescendo = wedgeStart.wedge.action == Crescendo;
						notation->parallelograms.append( vec2(X(wedgeStart), y+(-!crescendo-1)*3), vec2(x, y+(-crescendo-1)*3), 1.f);
						notation->parallelograms.append( vec2(X(wedgeStart), y+(!crescendo-1)*3), vec2(x, y+(crescendo-1)*3), 1.f);
					} else wedgeStart = sign;
				} else if(sign.type == Sign::Pedal) {
					int y = staffY(1, -24);
					if(sign.pedal.action == Ped) glyph(vec2(x, y), "pedal.Ped"_);
					if(sign.pedal.action == Start) pedalStart = x + glyphSize("pedal.Ped"_).x;
					if(sign.pedal.action == Change || sign.pedal.action == PedalStop) {
						{vec2 min(pedalStart, y), max(x, y+1);
							notation->fills.append(min, max-min);}
						if(sign.pedal.action == PedalStop) notation->fills.append(vec2(x-1, y-lineInterval), vec2(1, lineInterval));
						else {
							notation->parallelograms.append(vec2(x, y-1), vec2(x+noteSize.x/2, y-noteSize.x), 2.f);
							notation->parallelograms.append(vec2(x+noteSize.x/2, y-noteSize.x), vec2(x+noteSize.x, y), 2.f);
							pedalStart = x + noteSize.x;
						}
					}
				} else error(int(sign.type));
			}
		}
	}

	midiToSign = buffer<Sign>(midiNotes.size, 0);
	array<uint> chordExtra;

	constexpr bool logErrors = true;
	while(chordToNote.size<notes.size()) {
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
				vec2 p = notation->glyphs[note.glyphIndex].origin;
				text(p+vec2(noteSize.x, 2), "O"_+str(note.key), smallFont, debug);
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

		if(extraErrors > 18 /*FIXME: tremolo*/ || wrongErrors > 6 || missingErrors > 8 || orderErrors > 8) {
			//log("MID", midiNotes.slice(midiIndex,7));
			//log("XML", chord);
			break;
		}

		int match = chord.indexOf(midiKey);
		if(match >= 0) {
			Sign sign = chord.take(match); Note note = sign.note;
			assert_(note.key == midiKey);
			midiToSign.append( sign );
			vec2 p = notation->glyphs[note.glyphIndex].origin;
			text(p+vec2(noteSize.x, 2), str(note.key), smallFont, debug);
		} else if(chordExtra && chord.size == chordExtra.size) {
			int match = notes.values[chordToNote.size+1].indexOf(midiNotes[chordExtra[0]]);
			if(match >= 0) {
				assert_(chord.size<=3/*, chord*/);
				if(logErrors) log("-"_+str(apply(chord,[](const Sign& sign){return sign.note.key;})));
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
					vec2 p = notation->glyphs[note.glyphIndex].origin;
					text(p+vec2(noteSize.x, 2), str(note.key), smallFont, debug);
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
					vec2 p = notation->glyphs[note.glyphIndex].origin;
					text(p+vec2(noteSize.x, 2), str(note.key)+"?"_+str(midiKey)+"!"_, smallFont, debug);
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
			midiToSign.append();
			chordExtra.append( midiIndex );
		}
	}
	if(chordToNote.size == notes.size()) assert_(midiToSign.size == midiNotes.size);
	else {
		firstSynchronizationFailureChordIndex = chordToNote.size;
		notation->glyphs.append( move(debug) );
	}
	if(logErrors) log(extraErrors, wrongErrors, missingErrors, orderErrors);

    // Vertical center align
	vec2 offset = vec2(0, -staffY(0,16)+textFont.size);
	for(auto& o: notation->fills) o.origin += offset;
	assert_(!notation->blits);
	for(auto& o: notation->glyphs) o.origin += offset;
	for(auto& o: notation->parallelograms) o.min+=offset, o.max+=offset;
	assert_(!notation->lines);
	for(auto& o: notation->cubics) for(vec2& p: o.points) p+=vec2(offset);
}

inline bool operator ==(const Sign& sign, const uint& key) {
	assert_(sign.type == Sign::Note);
	return sign.note.key == key;
}

shared<Graphics> Sheet::graphics(int2 size) {
	notation->offset.y = (size.y - abs(sizeHint(size).y))/2;
	return share(notation);
}

int Sheet::measureIndex(int x) {
	if(x<measures[0]) return -1;
	for(uint i: range(measures.size-1)) if(measures[i]<=x && x<measures[i+1]) return i;
	assert_(x >= measures.last()); return measures.size;
}

int Sheet::stop(int unused axis, int currentPosition, int direction=0) {
	int currentIndex = measureIndex(currentPosition);
	return measures[clip(0, currentIndex+direction, int(measures.size-1))];
}
