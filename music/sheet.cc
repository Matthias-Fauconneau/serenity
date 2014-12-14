#include "sheet.h"
#include "notation.h"
#include "text.h"

static float glyph(vec2 origin, string name, Font& font, array<Glyph>& glyphs, float opacity=1) {
	uint index = font.index(name);
	glyphs.append(origin, font, index, index, black, opacity);
	return font.metrics(index).advance;
}

static uint centeredText(vec2 origin, string message, float size, array<Glyph>& glyphs) {
	Text text(message, size, 0, 1, 0, "LinLibertine");
	auto textGlyphs = move(text.graphics(0)->glyphs);
	for(auto& glyph: textGlyphs) { glyph.origin += origin - vec2(text.sizeHint(0))/2.f; glyphs.append(glyph); }
	return text.sizeHint(0).x;
}

static uint text(vec2 origin, string message, float size, array<Glyph>& glyphs) {
	Text text(message, size, 0, 1, 0, "LinLibertine");
	auto textGlyphs = move(text.graphics(0)->glyphs);
	for(auto& glyph: textGlyphs) { glyph.origin+=origin; glyphs.append(glyph); }
	return text.sizeHint(0).x;
}

// Layouts notations to graphic primitives (and parses notes to MIDI keys)
Sheet::Sheet(ref<Sign> signs, uint ticksPerQuarter, ref<uint> midiNotes, int2 pageSize) : pageSize(pageSize) {
	constexpr bool logErrors = false;
	map<uint, array<Sign>> notes; // Signs for notes (time, key, blitIndex)
	uint measureIndex=1, page=1, lineIndex=1, lineMeasureIndex=1;
	map<uint, Clef> clefs; KeySignature keySignature={0}; TimeSignature timeSignature={4,4};
    typedef array<Sign> Chord; // Signs belonging to a same chord (same time)
    Chord chords[2]; // Current chord (per staff)
    array<Chord> beams[2]; // Chords belonging to current beam (per staff) (also for correct single chord layout)
	uint beamStart[2] = {0,0};
	uint beatTime[2] = {0,0};
	//array<array<Sign>> pendingSlurs; // Slurs pending rendering (waiting for beams)
	//map<int, array<Sign>> slurs; // Signs belonging to current slur
	float pedalStart = 0; // Last pedal start/change position
	Sign wedgeStart {.wedge={}}; // Current wedge
	Sign octaveStart[2] {{.octave=OctaveStop}, {.octave=OctaveStop}}; // Current octave shift (for each staff)
	struct TieStart { uint key; vec2 position; };
	array<TieStart> activeTies[2];
	struct Position { // Holds current pen position for each line
		float staffs[2];
		float middle; // Dynamic, Wedge
		float metronome; // Metronome
		float octave; // OctaveShift
		float bottom; // Pedal
		/// Maximum position
		float maximum() { return max(max(staffs), max(middle, max(max(metronome, octave), bottom))); }
		/// Synchronizes staff positions to \a x
		void setStaves(float x) { for(float& staffX: staffs) staffX = max(staffX, x); }
		/// Synchronizes all positions to \a x
		void setAll(float x) { setStaves(x); middle = x; metronome = x; octave = x; bottom = x; }
    };
	map<int64, Position> timeTrack; // Maps times to positions

	pageBreaks.append(0);
	measureBars.insert(/*t*/0, /*x*/0.f);
	timeTrack.insert(/*t*/0u, /*x*/{{0,0},0,0,0,0});
	Graphics measure;
	vec2 offset = 0;
	int currentLineLowestStep = 0, currentLineHighestStep = 0;
	int previousLineLowestStep = 0;

	auto X = [&](const Sign& sign) -> float& {
		if(!timeTrack.contains(sign.time)) {
			size_t index = timeTrack.keys.linearSearch(sign.time);
			index = min(index, timeTrack.keys.size-1);
			assert_(index < timeTrack.keys.size);
			float x;
			if(sign.type == Sign::Wedge) x = timeTrack.values[index].middle;
			else if(sign.type == Sign::Metronome) x = timeTrack.values[index].metronome;
			else {
				assert_(sign.staff < 2, sign.staff, int(sign.type));
				x = timeTrack.values[index].staffs[sign.staff];
			}
			timeTrack.insert(sign.time, {{x,x},x,x,x,x});
		}
		assert_(timeTrack.contains(sign.time));
		float* x = 0;
		if(sign.type == Sign::Metronome) x = &timeTrack.at(sign.time).metronome;
		else if(sign.type == Sign::OctaveShift) x = &timeTrack.at(sign.time).octave;
		else if(sign.type==Sign::Dynamic || sign.type==Sign::Wedge) x = &timeTrack.at(sign.time).middle;
		else if(sign.type == Sign::Pedal) x = &timeTrack.at(sign.time).bottom;
		else {
			assert_(sign.staff < 2, sign.staff, int(sign.type));
			x = &timeTrack.at(sign.time).staffs[sign.staff];
		}
		return *x;
	};
	auto P = [&](const Sign& sign) { return vec2(X(sign), Y(sign)); };

	auto doBeam = [&](uint staff){
		auto& beam = beams[staff];
		if(!beam) return;

		uint tuplet = beam[0][0].note.tuplet;
		for(const Chord& chord: beam) for(Sign sign: chord) assert_(sign.note.tuplet == tuplet, sign.note.tuplet, tuplet, beam,
																	page, lineIndex, lineMeasureIndex);

		// Stems
		int sum = 0, count=0;
		for(Chord& chord: beam) {
			for(Sign& sign: chord) sum += clefStep(sign);
			count += chord.size;
		}
		bool stemUp = sum < -4*count; // sum/count<-4 (Average note height below mid staff)

		auto isDichord = [](const Chord& chord){
			for(const Sign& a: chord) for(const Sign& b: chord) if(a.note.key!=b.note.key && abs(a.note.step-b.note.step)<=1) return true;
			return false;
		};
		auto isTied = [](const Chord& chord){
			for(const Sign& a: chord) if(a.note.tie == Note::NoTie || a.note.tie == Note::TieStart) return false;
			return true;
		};

		if(beam.size==1) { // Draws single stem
			assert_(beam[0]);
			Sign sign = stemUp ? beam[0].last() : beam[0].first();
			String noteGlyphName = "noteheads.s"_+str(clip(0,int(sign.note.value-1),2)); // FIXME: factor, FIXME: assumes same width
			vec2 noteSize = glyphSize(noteGlyphName).x;
			float dx = (stemUp ? noteSize.x - 2 : (isDichord(beam[0])?noteSize.x:0));
			float x = X(sign) + dx;
			float yBottom = -inf, yTop = inf;
			for(Sign sign: beam[0]) if(sign.note.value >= Half) { yBottom = max(yBottom, Y(sign)); yTop = min(yTop, Y(sign)); } // inverted Y
			float yBase = stemUp ? yBottom : yTop;
			float yStem = stemUp ? yTop-stemLength : yBottom+stemLength;
			float opacity = isTied(beam[0]) ? 1./2 : 1;
			{vec2 min (x, ::min(yBase, yStem)), max(x+stemWidth, ::max(yBase, yStem));
				measure.fills.append(min, max-min, black, opacity); }
			/**/ if(sign.note.value==Eighth)
				glyph(vec2(x+stemWidth, yStem), stemUp?"flags.u3"_:"flags.d3"_, font, measure.glyphs, opacity);
			else if(sign.note.value==Sixteenth)
				glyph(vec2(x+stemWidth, yStem), stemUp?"flags.u4"_:"flags.d4"_, font, measure.glyphs, opacity);
			else if(sign.note.value==Thirtysecond)
				glyph(vec2(x+stemWidth, yStem), stemUp?"flags.u5"_:"flags.d5"_, font, measure.glyphs, opacity);
			else if(sign.note.value==Sixtyfourth)
				glyph(vec2(x+stemWidth, yStem), stemUp?"flags.u6"_:"flags.d6"_, font, measure.glyphs, opacity);
		} else if(beam.size==2) { // Draws slanted beam
			float x[2], base[2], tip[2];
			for(uint i: range(2)) {
				const Chord& chord = beam[i];
				Sign sign = chord.first();
				String noteGlyphName = "noteheads.s"_+str(clip(0,int(sign.note.value-1),2)); // FIXME: factor, FIXME: assumes same width
				vec2 noteSize = glyphSize(noteGlyphName).x;
				float dx = (stemUp ? noteSize.x - 2 : (isDichord(beam[i])?noteSize.x:0));
				x[i] = X(sign) + dx;
				base[i] = Y(stemUp?chord.first():chord.last());
				tip[i] = Y(stemUp?chord.last():chord.first())+(stemUp?-1:1)*stemLength;
			}
			float farTip = stemUp ? min(tip[0],tip[1]) : max(tip[0],tip[1]);
			float delta[2] = {clip(-lineInterval, tip[0]-farTip, lineInterval), clip(-lineInterval, tip[1]-farTip, lineInterval)};
			float dx[2];
			Sign sign[2] = { stemUp?beam[0].last():beam[0].first(), stemUp?beam[1].last():beam[1].first()};
			for(uint i: range(2)) {
				vec2 min(x[i], ::min(base[i],farTip+delta[i])), max(x[i]+stemWidth, ::max(base[i],farTip+delta[i]));
				float opacity = isTied(beam[i]) ? 1./2 : 1;
				measure.fills.append(min, max-min, black, opacity);
				String noteGlyphName = "noteheads.s"_+str(clip(0,int(sign[i].note.value-1),2)); // FIXME: factor, FIXME: assumes same width
				vec2 noteSize = glyphSize(noteGlyphName).x;
				dx[i] = (stemUp ? noteSize.x - 2 : (isDichord(beam[i])?noteSize.x:0));
			}
			float opacity = isTied(beam[0]) && isTied(beam[1]) ? 1./2 : 1;
			Value first = max(apply(beam[0], [](Sign sign){return sign.note.value;}));
			Value second = max(apply(beam[1], [](Sign sign){return sign.note.value;}));
			for(size_t index: range(min(first,second)-Quarter)) {
				float Y = (stemUp ? 1 : -1) * float(index) * (beamWidth+1);
				vec2 p0 (X(sign[0])+dx[0], farTip+delta[0]-beamWidth/2 + Y);
				vec2 p1 (X(sign[1])+dx[1]+stemWidth, farTip+delta[1]-beamWidth/2 + Y);
				measure.parallelograms.append(p0, p1, beamWidth, black, opacity);
			}
			for(size_t index: range(min(first,second)-Quarter, first-Quarter)) {
				float Y = (stemUp ? 1 : -1) * float(index) * (beamWidth+1);
				vec2 p0 (X(sign[0])+dx[0], farTip+delta[0]-beamWidth/2 + Y);
				vec2 p1 (X(sign[1])+dx[1]+stemWidth, farTip+delta[1]/2-beamWidth/2 + Y);
				p1 = (p0+p1)/2.f;
				measure.parallelograms.append(p0, p1, beamWidth, black, opacity);
			}
			for(size_t index: range(int(min(first,second)-Quarter), int(second-Quarter))) {
				float Y = (stemUp ? 1 : -1) * float(index) * (beamWidth+1);
				vec2 p0 (X(sign[0])+dx[0], farTip+delta[0]-beamWidth/2 + Y);
				vec2 p1 (X(sign[1])+dx[1]+stemWidth, farTip+delta[1]-beamWidth/2 + Y);
				p0 = (p0+p1)/2.f;
				measure.parallelograms.append(p0, p1, beamWidth, black, opacity);
			}
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
				String noteGlyphName = "noteheads.s"_+str(clip(0,int(sign.note.value-1),2)); // FIXME: factor, FIXME: assumes same width
				vec2 noteSize = glyphSize(noteGlyphName).x;
				float dx = (stemUp ? noteSize.x - 2 : (isDichord(chord)?noteSize.x:0));
				float x = X(sign) + dx;
				float y = Y(sign);
				{vec2 min(x, ::min(y, stemY)), max(x+stemWidth, ::max(stemY, y));
					float opacity = isTied(chord) ? 1./2 : 1;
					measure.fills.append(min, max-min, black, opacity); } // FIXME
			}
			// Beam
			for(size_t chordIndex: range(beam.size-1)) {
				const Chord& chord = beam[chordIndex];
				Value value = chord[0].note.value;
				for(size_t index: range(value-Quarter)) {
					float Y = stemY + (stemUp ? 1 : -1) * float(index) * (beamWidth+1);
					String noteGlyphName = "noteheads.s"_+str(clip(0,int(chord[0].note.value-1),2)); // FIXME: factor, FIXME: assumes same width
					vec2 noteSize = glyphSize(noteGlyphName).x;
					float dx = (stemUp ? noteSize.x - 2 : 0);
					vec2 min (X(chord[0]) + dx, Y-beamWidth/2+1), max(X(beam[chordIndex+1][0]) + dx + stemWidth, Y+beamWidth/2);
					measure.fills.append(min, max-min);
				}
			}
			// Tuplet
			if(tuplet) {
				float dx = (stemUp ? noteSize.x - 2 : 0);
				float x = (X(beam.first()[0])+X(beam.last()[0]))/2 + dx;
				float y = stemY + (stemUp ? -1 : 1) * 2 * beamWidth;
				centeredText(vec2(x,y), str(tuplet), 3*halfLineInterval, measure.glyphs);
			}
		}

		// Beams
		for(const Chord& chord: beam) {
			Sign sign = stemUp ? chord.first() : chord.last();
			float x = X(sign) + noteSize.x/2;
			int step = clefStep(sign);
			int y = Y(sign) + (stemUp?1:-1) * (lineInterval+(step%2?0:halfLineInterval));
			if(chord.first().note.staccato) { glyph(vec2(x,y),"scripts.staccato"_, font, measure.glyphs); y+=lineInterval; }
			if(chord.first().note.tenuto) { glyph(vec2(x,y),"scripts.tenuto"_, font, measure.glyphs); y+=lineInterval; }
			if(chord.first().note.trill) { glyph(vec2(x,y),"scripts.trill"_, font, measure.glyphs); y+=lineInterval; }
			int y2 = staffY(staff, stemUp ? -10 : 2);
			y = stemUp ? max(y,y2) : min(y,y2);
			y -= (stemUp?0:glyphSize("scripts.sforzato"_).y/2);
			if(chord.first().note.accent) { glyph(vec2(x,y),"scripts.sforzato"_, font, measure.glyphs); y+=lineInterval; }
		}
		beam.clear();

		/*// Slurs
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
		pendingSlurs.clear();*/
	};

	for(size_t signIndex: range(signs.size)) {
		{
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

			uint staff = sign.staff;
			if(staff < 2) { // Staff signs
				float x = X(sign);
				//assert_(x - measureBars.values.last() < 1024, sign);

				/**/ if(sign.type == Sign::OctaveShift) {
					/**/  if(sign.octave == Down) {
						x += text(vec2(x, staffY(1, 12+2)), "8"+superscript("va"), textSize, measure.glyphs);
						clefs[staff].octave++;
						//assert_(x - measureBars.values.last() < 1024);
						timeTrack.at(sign.time).octave = x;
					}
					else if(sign.octave == OctaveStop) {
						assert_(octaveStart[staff].octave==Down, int(octaveStart[staff].octave));
						clefs[staff].octave--;
						float start = X(octaveStart[staff]) + noteSize.x;
						float end = x;
						for(float x=start; x<end-noteSize.x; x+=noteSize.x*2) { // FIXME: breaks at each measure
							measure.fills.append(vec2(x,staffY(1, 12)),vec2(noteSize.x,1));
						}
					}
					else error(int(sign.octave));
					octaveStart[staff] = sign;
				}
				else {
					if(sign.type == Sign::Note) {
						Note& note = sign.note;
						note.clef = clefs.at(staff);
						note.measureIndex = measures.size();
						if(staff==0) {
							currentLineLowestStep = min(currentLineLowestStep, clefStep(sign));
							lowestStep = min(lowestStep, clefStep(sign));
						}
						if(staff==1) {
							currentLineHighestStep = max(currentLineHighestStep, clefStep(sign));
							highestStep = max(highestStep, clefStep(sign));
						}

						if(!note.grace && note.tie != Note::Merged) { //FIXME: render ties

							//if(slurs.contains(int(staff))) slurs.at(int(staff)).append( sign );
							//if(slurs.contains(-1)) slurs.at(-1).append( sign );

							String noteGlyphName = "noteheads.s"_+str(clip(0,int(note.value-1),2)); // FIXME: factor
							x += 2*glyphSize(noteGlyphName).x;
							for(Sign sign: chords[staff]) if(abs(sign.note.step-note.step) <= 1) { x += glyphSize(noteGlyphName).x; break; }// Dichord
							for(Sign sign: chords[staff]) if(sign.note.dot) x += glyphSize(noteGlyphName).x; // Dot
							if(chords[staff]) sign.note.tuplet = chords[staff][0].note.tuplet; // Does not affect rendering but mismatch triggers asserts
							chords[staff].insertSorted(sign);
						} else {
							//if(note.slash) measure.parallelograms.append( Parallelogram(p+vec2(-dx+dx/2,dx), p+vec2(dx+dx/2,-dx), 1) );
							//FIXME: render graces
						}
					}
					else if(sign.type == Sign::Rest) {
						doBeam(staff);
						vec2 p = vec2(x, staffY(staff, -4));
						if(sign.rest.value == Double) x+= 3*glyph(p, "rests.M2"_, font, measure.glyphs);
						else if(sign.rest.value == Whole) x+= 3*glyph(p, "rests.0"_, font, measure.glyphs);
						else if(sign.rest.value == Half) x+= 3*glyph(p, "rests.1"_, font, measure.glyphs);
						else if(sign.rest.value == Quarter) x+= 3*glyph(p, "rests.2"_, font, measure.glyphs);
						else if(sign.rest.value == Eighth) x+= 3*glyph(p, "rests.3"_, font, measure.glyphs);
						else if(sign.rest.value == Sixteenth) x+= 3*glyph(p, "rests.4"_, font, measure.glyphs);
						else if(sign.rest.value == Thirtysecond) x+= 3*glyph(p, "rests.5"_, font, measure.glyphs);
						else if(sign.rest.value == Sixtyfourth) x+= 3*glyph(p, "rests.6"_, font, measure.glyphs);
						else error(int(sign.rest.value));
						uint measureLength = timeSignature.beats*quarterDuration;
						beatTime[staff] += sign.rest.value == Whole ? measureLength /*FIXME: only if single*/ : sign.rest.duration();
					}
					else if(sign.type == Sign::Clef) {
						Clef clef = sign.clef;
						assert_(!clef.octave);
						if((!clefs.contains(staff) || clefs.at(staff).clefSign != sign.clef.clefSign) && !(signIndex>=signs.size-2)) {
							String glyphName = (clef.clefSign==Treble ? "clefs.G" : "clefs.F")+(clefs.contains(staff)?"_change"_:""_);
							x += clefs.contains(staff) ? -glyphSize(glyphName).x : noteSize.x;
							x += glyph(vec2(x, staffY(staff, clef.clefSign==Treble ? -6 : -2)), glyphName, font, measure.glyphs);
							x += clefs.contains(staff) ? noteSize.x/2 : noteSize.x;
							clefs[staff] = sign.clef;
						}
					}
					else error(int(sign.type));

					if(sign.duration) { // Updates end position for future signs
						for(size_t index: range(timeTrack.size())) { // Updates positions of any interleaved notes
							if(timeTrack.keys[index] > sign.time && timeTrack.keys[index] <= sign.time+sign.duration) {
								timeTrack.values[index].setStaves(x);
							}
						}
						if(timeTrack.contains(sign.time+sign.duration)) timeTrack.at(sign.time+sign.duration).setStaves(x);
						else timeTrack.insert(sign.time+sign.duration, {{x,x},x,x,x,x});
					} else timeTrack.at(sign.time).staffs[staff] = x;
				}
			} else {
				assert_(staff == uint(-1));
				assert_(sign.duration == 0);
				if(sign.type == Sign::Measure || sign.type==Sign::KeySignature || sign.type==Sign::TimeSignature) { // Clearing signs (across staves)
					//float x = X(sign); X(sign) -> float& but maximum() -> float
					if(!timeTrack.contains(sign.time)) { // FIXME: -> X
						log("!timeTrack.contains(sign.time)", sign.time);
						size_t index = timeTrack.keys.linearSearch(sign.time);
						index = min(index, timeTrack.keys.size-1);
						assert_(index < timeTrack.keys.size);
						float x = timeTrack.values[index].maximum();
						timeTrack.insert(sign.time, {{x,x},x,x,x,x});
					}
					float x = timeTrack.at(sign.time).maximum();

					if(sign.type==Sign::TimeSignature) {
						for(size_t staff: range(staffCount)) {
							/*assert_(beatTime[staff] % (timeSignature.beats*quarterDuration) == 0,
									withName(beatTime[staff], beatTime[staff] % (timeSignature.beats*quarterDuration),
											 timeSignature.beats, quarterDuration, page, lineIndex, lineMeasureIndex, staff));*/
							beatTime[staff] = 0;
						}

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
							page = sign.measure.page;
							lineIndex = sign.measure.pageLine;
							lineMeasureIndex = sign.measure.lineMeasure;
							{vec2 min(x-barWidth+barWidth/2, staffY(1,0)), max(x+barWidth/2, staffY(0,-8));
								measure.fills.append(min, max-min); } // Bar
							// Raster
							for(int staff: range(staffCount)) {
								for(int line: range(5)) {
									int y = staffY(staff, -line*2);
									{vec2 min(measureBars.values.last(), y), max(x, y+lineWidth);
										measure.fills.append(min, max-min);}
								}
							}
							for(uint staff: range(staffCount)) doBeam(staff); // Before measure is moved
							// FIXME: layout with measure relative coordinates instead of translating back
							if(offset.x+x > pageSize.x) {
								offset.x = -measureBars.values.last();
								offset.y += staffY(0, previousLineLowestStep-7) - staffY(1, currentLineHighestStep+7);
								previousLineLowestStep = currentLineLowestStep;
								currentLineLowestStep = 0, currentLineHighestStep = 0;
								if(offset.y + staffY(0, previousLineLowestStep-7) > pageSize.y) {
									offset.y = 0;
									pageBreaks.append(measures.size());
								}
							}
							measure.translate(offset);
							measures.insertMulti(Rect(int2(offset)+int2(measureBars.values.last()-barWidth+barWidth/2, 0), int2(offset)+int2(x+barWidth/2, 0)),
												 shared<Graphics>(move(measure)) );
							assert_(sign.time - measureBars.keys.last() <= timeSignature.beats*ticksPerQuarter);
							measureBars.insert(sign.time, x);
							x += noteSize.x;
							text(vec2(x, staffY(1, 12)), str(page)+','+str(lineIndex)+','+str(lineMeasureIndex)+' '+str(measureIndex), textSize,
								 debug->glyphs);
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
						if(ticksPerMinutes!=int64(sign.metronome.perMinute*ticksPerQuarter)) {
							x += text(vec2(x, staffY(1, 12)), "♩="_+str(sign.metronome.perMinute)+" "_, textSize, measure.glyphs);
							if(ticksPerMinutes) log(ticksPerMinutes, "->", int64(sign.metronome.perMinute*ticksPerQuarter)); // FIXME: variable tempo
							ticksPerMinutes = max(ticksPerMinutes, int64(sign.metronome.perMinute*ticksPerQuarter));
						}
					}
					else if(sign.type == Sign::Dynamic) {
						string word = sign.dynamic;
						float w = 0;
						for(char character: word.slice(0,word.size-1)) w += font.metrics(font.index(string{character})).advance;
						w += glyphSize({word.last()}).x;
						x -= w/2; x += glyphSize({word[0]}).x/2;
						for(char character: word) {
							x += glyph(vec2(x, (staffY(1, -8)+staffY(0, 0))/2), {character}, font, measure.glyphs);
						}
					} else if(sign.type == Sign::Wedge) {
						int y = (staffY(1, -8)+staffY(0, 0))/2;
						if(sign.wedge == WedgeStop) {
							bool crescendo = wedgeStart.wedge == Crescendo;
							measure.parallelograms.append( vec2(X(wedgeStart), y+(-!crescendo-1)*3), vec2(x, y+(-crescendo-1)*3), 1.f);
							measure.parallelograms.append( vec2(X(wedgeStart), y+(!crescendo-1)*3), vec2(x, y+(crescendo-1)*3), 1.f);
						} else wedgeStart = sign;
					} else if(sign.type == Sign::Pedal) {
						int y = staffY(0, -20);
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

		for(uint staff: range(staffCount)) { // Layout notes, stems, beams and slurs
			array<Chord>& beam = beams[staff];

			// Notes
			Chord& chord = chords[staff];
			if(chord && (signIndex==signs.size-1 || signs[signIndex+1].time != chord.last().time)) {

				Value value = chord[0].note.value;
				uint duration = chord[0].note.duration();
				for(Sign sign: chord) {
					assert_(sign.type==Sign::Note);
					Note& note = sign.note;
					duration = min(duration, note.duration());
					value = max(value, note.value);
				}

				uint beamDuration = 0;
				for(Chord& chord: beam) {
					assert_(chord);
					uint duration = chord[0].note.duration();
					for(Sign& sign: chord) {
						assert(sign.type == Sign::Note);
						duration = min(duration, sign.note.duration());
					}
					beamDuration += duration;
				}
				beamDuration += duration;

				uint beatDuration = quarterDuration; //*4/timeSignature.beatUnit;
				//uint beamFirstBeat = beamStart[staff] / beatDuration;
				//uint beamLastBeat = (beamStart[staff]+beamDuration) / beatDuration;

				if(beam &&
						( beamDuration > beatDuration /*Beam before too long*/
						  /*|| beamFirstBeat!=beamLastBeat*/ /*Beam before spanning*/
						  || beatTime[staff]%beatDuration==0 /*"Beam" before spanning (single chord)*/
						  /*|| beam.last().last().note.duration() < duration*/ /*Increasing time (FIXME: revert last if different*/
						  || beam[0][0].note.tuplet != chord[0].note.tuplet
						  )) doBeam(staff);

				buffer<bool> shift (chord.size); shift.clear();
				int lastStep = chord[0].note.step;
				for(size_t index: range(1, chord.size)) { // Bottom to top (TODO: same for accidentals)
					Sign sign = chord[index];
					Note& note = sign.note;
					if(note.step==lastStep+1) shift[index] = !shift[index-1];
					lastStep = note.step;
				}
				if(shift.contains(true)) {
					for(size_t index: range(chord.size-1)) { // Alternate
						if(shift[index] == shift[index+1] && abs(chord[index].note.step-chord[index+1].note.step)>2) shift[index] = !shift[index];
					}
					if(chord.size>1 && abs(chord.last().note.step-chord[chord.size-2].note.step)>=2) shift.last() = true; // Shifts single top
				}
				float accidentalAboveY = -inf, accidentalOffset = 0;
				for(size_t index: reverse_range(chord.size)) { // Top to bottom (notes are sorted bass to treble)
					Sign sign = chord[index];
					assert_(sign.type==Sign::Note);
					Note& note = sign.note;
					//assert_(note.measureIndex == measures.size());
					const float x = X(sign) + (shift[index] ? noteSize.x : 0), y = Y(sign);
					String noteGlyphName = "noteheads.s"_+str(clip(0,int(note.value-1),2)); // FIXME: factor
					vec2 noteSize = glyphSize(noteGlyphName);
					float opacity = (note.tie == Note::NoTie || note.tie == Note::TieStart) ? 1 : 1./2;

					// Ledger lines
					int step = clefStep(sign);
					for(int s=2; s<=step; s+=2) {
						int y=staffY(staff, s); measure.fills.append(vec2(x-noteSize.x/3,y),vec2(noteSize.x*5/3,1), black, opacity);
					}
					for(int s=-10; s>=step; s-=2) {
						int y=staffY(staff, s); measure.fills.append(vec2(x-noteSize.x/3,y),vec2(noteSize.x*5/3,1), black, opacity);
					}
					// Body
					note.glyphIndex = measure.glyphs.size;
					glyph(vec2(x, y), noteGlyphName, note.grace?graceFont:font, measure.glyphs, opacity);
					// Accidental
					if(note.accidental) {
						if(abs(y-accidentalAboveY)<=3*halfLineInterval) accidentalOffset -= noteSize.x/2;
						else accidentalOffset = 0;
						assert_(size_t(note.accidental-1) < 5, int(note.accidental));
						String name = "accidentals."_+accidentalNamesLy[note.accidental];
						glyph(vec2(x-glyphSize(name).x+accidentalOffset, y), name, font, measure.glyphs); //TODO: highlight as well
						accidentalAboveY = y;
					}
					if(note.tie == Note::NoTie || note.tie == Note::TieStart) notes.sorted(sign.time).append( sign );
				}
				for(size_t index: reverse_range(chord.size)) {
					Sign sign = chord[index];
					assert_(sign.type==Sign::Note);
					Note& note = sign.note;

					if(sign.note.tie == Note::TieContinue || sign.note.tie == Note::TieStop) {
						size_t tieStart = invalid;
						for(size_t index: range(activeTies[staff].size)) {
							if(activeTies[staff][index].key == note.key) {
								assert_(tieStart==invalid, note.key, apply(activeTies[staff],[](TieStart o){return  o.key;}));
								tieStart = index;
							}
						}
						if(tieStart != invalid) {
							assert_(tieStart != invalid, note.key, apply(activeTies[staff],[](TieStart o){return  o.key;}), signIndex);
							TieStart tie = activeTies[staff].take(tieStart);

							assert_(tie.position.y == P(sign).y);
							float y = P(sign).y;
							int slurDown = (chord.size>1 && index==chord.size-1) ? -1 :(y > staffY(staff, -4) ? 1 : -1);
							vec2 p0 = vec2(tie.position.x + noteSize.x + (note.dot?noteSize.x/2:0), y + slurDown*halfLineInterval);
							float bodyOffset = shift[index] ? noteSize.x : 0; // Shift body for dichords
							vec2 p1 = vec2(P(sign).x + bodyOffset, y + slurDown*halfLineInterval);
							const float offset = min(halfLineInterval, (P(sign).x-tie.position.x)/4), width = halfLineInterval/2;
							vec2 k0 (p0.x, p0.y + slurDown*offset);
							vec2 k0p (k0.x, k0.y + slurDown*width);
							vec2 k1 (p1.x, p1.y + slurDown*offset);
							vec2 k1p (k1.x, k1.y + slurDown*width);
							measure.cubics.append(Cubic(copyRef(ref<vec2>({p0,k0,k1,p1,k1p,k0p}))));
						} else sign.note.tie = Note::NoTie;
					}
					if(sign.note.tie == Note::TieStart || sign.note.tie == Note::TieContinue) {
						float bodyOffset = shift[index] ? noteSize.x : 0; // Shift body for dichords
						activeTies[staff].append(note.key, P(sign)+vec2(bodyOffset, 0));
					}

					note.step = note.step/2*2 +1; // Aligns dots between lines (WARNING: moves note in this scope)
					String noteGlyphName = "noteheads.s"_+str(clip(0,int(note.value-1),2)); // FIXME: factor
					vec2 noteSize = glyphSize(noteGlyphName);
					if(note.dot) {
						 float opacity = (note.tie == Note::NoTie || note.tie == Note::TieStart) ? 1 : 1./2;
						glyph(P(sign)+vec2((shift.contains(true) ? noteSize.x : 0)+noteSize.x*4/3,0),"dots.dot"_, font, measure.glyphs, opacity);
					}
				}
				if(value >= Half /*also quarter and halfs for stems*/) {
					if(!beam) beamStart[staff] = beatTime[staff];
					if(value <= Quarter) doBeam(staff); // Flushes any pending beams
					beam.append(move(chord));
					if(value <= Quarter) doBeam(staff); // Only stems for quarter and halfs (actually no beams :/)
				}
				chord.clear();
				beatTime[staff] += duration;
				/*if(sign.type == Sign::Measure) {
					uint measureLength = timeSignature.beats*quarterDuration;
					assert_(beatTime[staff]%measureLength==0
							|| duration > measureLength
							|| ((beatTime[staff]-duration)/measureLength != beatTime[staff]/measureLength),
							withName(beatTime[staff], beatTime[staff]%measureLength, measureLength, page, lineIndex, lineMeasureIndex, staff, duration));
				}*/
			}
		}
	}
	pageBreaks.append(measures.size());
	if(!ticksPerMinutes) ticksPerMinutes = 120*ticksPerQuarter; // TODO: default tempo from audio

	midiToSign = buffer<Sign>(midiNotes.size, 0);
	array<uint> chordExtra;

	if(midiNotes) while(chordToNote.size < notes.size()) {
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
				if(note.measureIndex != invalid && note.glyphIndex != invalid) {
					vec2 p = measures.values[note.measureIndex]->glyphs[note.glyphIndex].origin;
					text(p+vec2(noteSize.x, 2), "O"_+str(note.key), 12, debug->glyphs);
				}
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
			if(note.measureIndex != invalid && note.glyphIndex != invalid) {
				assert_(note.measureIndex < measures.size(), note.measureIndex, measures.size());
				vec2 p = measures.values[note.measureIndex]->glyphs[note.glyphIndex].origin;
				text(p+vec2(noteSize.x, 2), str(note.key), 12, debug->glyphs);
			}
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
					if(note.measureIndex != invalid && note.glyphIndex != invalid) {
						vec2 p = measures.values[note.measureIndex]->glyphs[note.glyphIndex].origin;
						text(p+vec2(noteSize.x, 2), str(note.key), 12, debug->glyphs);
					}
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
					if(note.measureIndex != invalid && note.glyphIndex != invalid) {
						vec2 p = measures.values[note.measureIndex]->glyphs[note.glyphIndex].origin;
						text(p+vec2(noteSize.x, 2), str(note.key)+"?"_+str(midiKey)+"!"_, 12, debug->glyphs);
					}
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
	if(chordToNote.size == notes.size() || !midiNotes) {} //assert_(midiToSign.size == midiNotes.size, midiToSign.size, midiNotes.size); FIXME
	else { firstSynchronizationFailureChordIndex = chordToNote.size; }
	if(logErrors && (extraErrors||wrongErrors||missingErrors||orderErrors)) log(extraErrors, wrongErrors, missingErrors, orderErrors);

	if(pageSize) {
		//TODO: h justify, v distribute
	} else {
		auto verticalAlign = [&](Graphics& measure) {
			vec2 offset = vec2(0, -staffY(1,highestStep)+textSize);
			measure.translate(offset);
		};
		for(Graphics& measure: measures.values) verticalAlign(measure);
		verticalAlign(debug);
	}
}

inline bool operator ==(const Sign& sign, const uint& key) {
	assert_(sign.type == Sign::Note);
	return sign.note.key == key;
}

shared<Graphics> Sheet::graphics(int2 size, Rect clip) {
	shared<Graphics> graphics;
	size_t first = 0, last;
	if(pageSize) {
		first = pageBreaks[pageIndex];
		last = pageBreaks[pageIndex+1];
	}
	else {
		for(; first < measures.size(); first++) if(measures.keys[first].max.x > clip.min.x) break;
		last = first;
		for(; last < measures.size(); last++) if(measures.keys[last].min.x > clip.max.x) break;
	}
	for(const auto& measure: measures.values.slice(first, last-first)) graphics->graphics.insertMulti(vec2(0), share(measure));
	if(!pageSize) graphics->offset.y = (size.y - abs(sizeHint(size).y))/2;
	else graphics->offset.y = -staffY(1, highestStep+8);
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

bool Sheet::keyPress(Key key, Modifiers) {
	if(key==LeftArrow) { pageIndex = max(0, int(pageIndex)-1); return true; }
	if(key==RightArrow) { pageIndex = min(pageIndex+1, pageBreaks.size-2); return true; }
	return false;
}
