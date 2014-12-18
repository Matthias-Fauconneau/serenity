#include "sheet.h"
#include "notation.h"
#include "text.h"

static float glyph(vec2 origin, uint code, Font& font, array<Glyph>& glyphs, float opacity=1) {
	uint index = font.index(code);
	glyphs.append(origin, font, index, index, black, opacity);
	return font.metrics(index).advance;
}

// Layouts notations to graphic primitives (and parses notes to MIDI keys)
Sheet::Sheet(ref<Sign> signs, uint ticksPerQuarter, ref<uint> midiNotes, float halfLineInterval, int2 pageSize, string title)
	: halfLineInterval(halfLineInterval), pageSize(pageSize) {
	constexpr bool logErrors = false;

	//uint measureIndex=1, page=1, lineIndex=1, lineMeasureIndex=1;
	Clef clefs[2] = {{NoClef,0}, {NoClef,0}}; KeySignature keySignature={0}; TimeSignature timeSignature={4,4};
    typedef array<Sign> Chord; // Signs belonging to a same chord (same time)
    Chord chords[2]; // Current chord (per staff)
    array<Chord> beams[2]; // Chords belonging to current beam (per staff) (also for correct single chord layout)
	uint beamStart[2] = {0,0};
	uint beatTime[2] = {0,0};
	float pedalStart = 0; // Last pedal start/change position
	Sign wedgeStart {.wedge={}}; // Current wedge
	Sign octaveStart[2] {{.octave=OctaveStop}, {.octave=OctaveStop}}; // Current octave shift (for each staff)
	struct TieStart { uint key; vec2 position; };
	array<TieStart> activeTies[2];
	array<Chord> tuplets[2];

	map<uint, array<Sign>> notes; // Signs for notes (time, key, blitIndex)

	measureBars.insert(/*t*/0, /*x*/0.f);
	float offsetY = 0;
	//pageBreaks.append(0);
	//array<int> lineBreaks;
	//lineBreaks.append(0);
	//array<shared<Graphics>> measures;
	array<Graphics> systems;

	auto text = [this](vec2 origin, string message, float size, array<Glyph>& glyphs) {
	   Text text(message, size, 0, 1, 0, textFont);
	   auto textGlyphs = move(text.graphics(0)->glyphs);
	   for(auto& glyph: textGlyphs) { glyph.origin+=origin; glyphs.append(glyph); }
	   return text.sizeHint(0).x;
   };

	auto doPage = [&]{
		if(offsetY < pageSize.y) { // Spreads systems with margins
			float extra = (pageSize.y - offsetY) / (systems.size+1);
			float offset = extra;
			for(Graphics& system: systems) {
				system.translate(vec2(0, offset));
				offset += extra;
			}
		}
		if(offsetY > pageSize.y) { // Spreads systems without margins
			float extra = (pageSize.y - offsetY) / (systems.size-1);
			float offset = 0;
			for(Graphics& system: systems) {
				system.translate(vec2(0, offset));
				offset += extra;
			}
		}
		pages.append();
		for(const Graphics& system: systems) pages.last().append(system); // Appends systems first to preserve references to glyph indices
		if(pages.size==1) {
			int2 size = Text(bold(title),textSize,0,1,0,textFont).sizeHint(0);
			text(vec2(pageSize.x/2-size.x/2,0), bold(title), textSize, pages.last().glyphs);
		}
		{// Page index at each corner
			int2 size = Text(str(pages.size),textSize,0,1,0,textFont).sizeHint(0);
			text(vec2(margin,margin), str(pages.size), textSize, pages.last().glyphs);
			text(vec2(pageSize.x-size.x-margin,margin), str(pages.size), textSize, pages.last().glyphs);
			text(vec2(margin,pageSize.y-size.y-margin/2), str(pages.size), textSize, pages.last().glyphs);
			text(vec2(pageSize.x-size.x-margin,pageSize.y-size.y-margin/2), str(pages.size), textSize, pages.last().glyphs);
		}
		systems.clear();
	};

	size_t lineCount = 0;//, pageLineCount = 0;
	for(size_t startIndex = 0; startIndex < signs.size;) { // Lines
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
		{float x = /*2**/0; timeTrack.insert(signs[startIndex].time, {{x,x},x,x,x,x});}

		auto X = [&timeTrack](const Sign& sign) -> float& {
			if(!timeTrack.contains(sign.time)) {
				size_t index = timeTrack.keys.linearSearch(sign.time);
				index = min(index, timeTrack.keys.size-1);
				assert_(index < timeTrack.keys.size);
				float x;
				if(sign.type == Sign::Wedge) x = timeTrack.values[index].middle;
				else if(sign.type == Sign::Metronome) x = timeTrack.values[index].metronome;
				else if(sign.type == Sign::Pedal) x = timeTrack.values[index].bottom;
				else {
					assert_(sign.staff < 2, sign.staff==uint(-1), int(sign.type));
					x = timeTrack.values[index].staffs[sign.staff];
				}
				timeTrack.insert(sign.time, {{x,x},x,x,x,x});
			}
			assert_(timeTrack.contains(sign.time), sign.time, timeTrack.keys);
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

		// Evaluates line break position, total fixed width and space count
		size_t breakIndex = startIndex;
		float allocatedLineWidth = 0;
		uint spaceCount = 0;
		uint measureCount = 0;
		uint additionnalSpaceCount = 0;
		bool pageBreak = false;
		for(size_t signIndex: range(startIndex, signs.size)) {
			Sign sign = signs[signIndex];

			uint staff = sign.staff;
			if(staff < 2) { // Staff signs
				float x = X(sign);
				if(sign.type == Sign::Note) {
					Note& note = sign.note;
					assert_(note.tie != Note::Merged);
					if(!note.grace) {
						x += glyphAdvance(SMuFL::NoteHead::Black);
						for(Sign sign: chords[staff]) if(abs(sign.note.step-note.step) <= 1) { x += glyphAdvance(SMuFL::NoteHead::Black); break; }// Dichord
						//for(Sign sign: chords[staff]) if(sign.note.dot) x += glyphAdvance(noteGlyphName); // Dot
						chords[staff].insertSorted(sign);
						x += space;
					}
				}
				else if(sign.type == Sign::Rest) {
					x += glyphAdvance(SMuFL::Rest::Whole+int(sign.rest.value), &smallFont);
					x += space;
				}
				else if(sign.type == Sign::Clef) {
					Clef clef = sign.clef;
					assert_(clef.clefSign);
					x += glyphAdvance(clef.clefSign);
					x += space;
				}
				if(sign.duration) { // Updates end position for future signs
					for(size_t index: range(timeTrack.size())) { // Updates positions of any interleaved notes
						if(timeTrack.keys[index] > sign.time && timeTrack.keys[index] <= sign.time+sign.duration) {
							timeTrack.values[index].setStaves(x);
						}
					}
					if(timeTrack.contains(sign.time+sign.duration)) timeTrack.at(sign.time+sign.duration).setStaves(x);
					else {
						timeTrack.insert(sign.time+sign.duration, {{x,x},x,x,x,x});
						//spaceCount++; //== timeTrack.size()
					}
				} else timeTrack.at(sign.time).staffs[staff] = x;
			} else {
				assert_(staff == uint(-1)  && sign.duration == 0);
				//float x = X(sign); X(sign) -> float& but maximum() -> float
				if(!timeTrack.contains(sign.time)) { // FIXME: -> X
					log("!timeTrack.contains(sign.time)", sign.time);
					size_t index = timeTrack.keys.linearSearch(sign.time);
					index = min(index, timeTrack.keys.size-1);
					assert_(index < timeTrack.keys.size);
					float x = timeTrack.values[index].maximum();
					timeTrack.insert(sign.time, {{x,x},x,x,x,x});
				}
				assert_(timeTrack.contains(sign.time), sign.time);
				float x = timeTrack.at(sign.time).maximum();

				if(sign.type==Sign::TimeSignature) {
					x += 2*glyphAdvance(SMuFL::TimeSignature::_0);
					timeTrack.at(sign.time).setStaves(x); // Does not clear directions lines
				} else if(sign.type == Sign::Measure || sign.type==Sign::KeySignature) { // Clears all lines (including direction lines)
					if(sign.type == Sign::Measure) {
						//if(x > pageSize.x) break;
						if(pageSize && breakIndex>startIndex && sign.measure.lineBreak) break;
						if(sign.measure.lineBreak == PageBreak) pageBreak = true;
						breakIndex = signIndex+1; // Includes measure break
						x += space;
						allocatedLineWidth = x;
						spaceCount = timeTrack.size() + measureCount + additionnalSpaceCount;
						//uint manualBreak[] = {6,6,8,8,8,8, 8,8,8,8,8,8, 8,8,8,8,8,8}; //TODO: align 2,4 <-> 3,6
						measureCount++;
						//if(lineCount < ref<uint>(manualBreak).size && measureCount>=manualBreak[lineCount]) break;
					}
					else if(sign.type==Sign::KeySignature) {
						x += abs(sign.keySignature.fifths)*glyphAdvance(sign.keySignature.fifths<0 ? Accidental::Flat : Accidental::Sharp);
						x += space;
						additionnalSpaceCount++;
					}
					else error(int(sign.type));
					timeTrack.at(sign.time).setAll(x);
				}
			}

			for(uint staff: range(staffCount)) {
				Chord& chord = chords[staff];
				if(chord && (signIndex==signs.size-1 || signs[signIndex+1].time != chord.last().time)) chord.clear();
			}
		}
		assert_(startIndex < breakIndex && spaceCount, startIndex, breakIndex, spaceCount);

		// Evaluates vertical bounds
		int currentLineLowestStep = 7, currentLineHighestStep = -7; bool lineHasTopText = false;
		for(size_t signIndex: range(startIndex, breakIndex)) {
			Sign sign = signs[signIndex];
			uint staff = sign.staff;
			if(sign.type==Sign::Repeat) lineHasTopText=true;
			if(sign.type == Sign::Note) {
				if(staff==0) {
					currentLineLowestStep = min(currentLineLowestStep, clefStep(sign));
					lowestStep = min(lowestStep, clefStep(sign));
				}
				if(staff==1) {
					currentLineHighestStep = max(currentLineHighestStep, clefStep(sign));
					highestStep = max(highestStep, clefStep(sign));
				}
			}
		}

		// Evaluates vertical offset
		int highMargin = 3, lowMargin = -8-4;
		//uint manualBreak[] = {5,6,7};
		if(/*offsetY + staffY(0, currentLineLowestStep-7)-staffY(1, max(highMargin,currentLineHighestStep)+7) > pageSize.y ||*/
			/*(pages.size < ref<uint>(manualBreak).size && systems.size>=manualBreak[pages.size])*/
				pageBreak && systems) {
			doPage();

			offsetY = 0;
			//pageLineCount = 0;
		}
		offsetY += -staffY(1, max(highMargin/*lineHasTopText?4:0*/,currentLineHighestStep)+7);

		auto isDichord = [](const Chord& chord){
			for(const Sign& a: chord) for(const Sign& b: chord) if(a.note.key!=b.note.key && abs(a.note.step-b.note.step)<=1) return true;
			return false;
		};
		auto noteCode = [](const Sign& sign) { return min(SMuFL::NoteHead::Whole+int(sign.note.value), int(SMuFL::NoteHead::Black)); };
		auto noteSize = [&noteCode, this](const Sign& sign) { return font.metrics(font.index(noteCode(sign))).advance; };
		auto stemX = [&](const Chord& chord, bool stemUp) {
			return X(chord[0]) + (stemUp||isDichord(chord) ? noteSize(chord[0]) : 0);
		};

		Graphics system;
		auto doBeam = [&](uint staff) {
			auto& beam = beams[staff];
			if(!beam) return;

			// Stems
			int sum = 0, count=0;
			for(Chord& chord: beam) {
				for(Sign& sign: chord) sum += clefStep(sign);
				count += chord.size;
			}
			bool stemUp = sum < -4*count; // sum/count<-4 (Average note height below mid staff)

			auto isTied = [](const Chord& chord){
				for(const Sign& a: chord) if(a.note.tie == Note::NoTie || a.note.tie == Note::TieStart) return false;
				return true;
			};

			if(beam.size==1) { // Draws single stem
				assert_(beam[0]);
				Sign sign = stemUp ? beam[0].last() : beam[0].first();
				float yBottom = -inf, yTop = inf;
				for(Sign sign: beam[0]) if(sign.note.value >= Half) { yBottom = max(yBottom, Y(sign)); yTop = min(yTop, Y(sign)); } // inverted Y
				float yBase = stemUp ? yBottom-1./2 : yTop+1./2;
				float yStem = stemUp ? yTop-stemLength : yBottom+stemLength;
				float x = stemX(beam[0], stemUp);
				float opacity = isTied(beam[0]) ? 1./2 : 1;
				system.lines.append(vec2(x, ::min(yBase, yStem)), vec2(x, max(yBase, yStem)), black, opacity);
				if(sign.note.value>=Eighth)
					glyph(vec2(x, yStem), (int(sign.note.value)-Eighth)*2 + (stemUp ? SMuFL::Flag::Above : SMuFL::Flag::Below), smallFont, system.glyphs, opacity);
			} else if(beam.size==2) { // Draws pairing beam
				float x[2], base[2], tip[2];
				for(uint i: range(2)) {
					const Chord& chord = beam[i];
					x[i] = stemX(chord, stemUp);
					base[i] = Y(stemUp?chord.first():chord.last()) + (stemUp ? -1./2 : +1./2);
					tip[i] = Y(stemUp?chord.last():chord.first())+(stemUp?-1:1)*stemLength;
				}
				float midTip = (tip[0]+tip[1])/2; //farTip = stemUp ? min(tip[0],tip[1]) : max(tip[0],tip[1]);
				float delta[2] = {clip(-lineInterval, tip[0]-midTip, lineInterval), clip(-lineInterval, tip[1]-midTip, lineInterval)};
				Sign sign[2] = { stemUp?beam[0].last():beam[0].first(), stemUp?beam[1].last():beam[1].first()};
				for(uint i: range(2)) {
					float opacity = isTied(beam[i]) ? 1./2 : 1;
					tip[i] = midTip+delta[i];
					system.lines.append(vec2(x[i], ::min(base[i],tip[i])), vec2(x[i], ::max(base[i],tip[i])), black, opacity);
				}
				float opacity = isTied(beam[0]) && isTied(beam[1]) ? 1./2 : 1;
				Value first = max(apply(beam[0], [](Sign sign){return sign.note.value;}));
				Value second = max(apply(beam[1], [](Sign sign){return sign.note.value;}));
				// Beams
				for(size_t index: range(min(first,second)-Quarter)) {
					float Y = (stemUp ? 1 : -1) * float(index) * (beamWidth+1);
					vec2 p0 (x[0]-stemWidth/2, tip[0]-beamWidth/2 + Y);
					vec2 p1 (x[1]+stemWidth/2, tip[1]-beamWidth/2 + Y);
					system.parallelograms.append(p0, p1, beamWidth, black, opacity);
				}
				for(size_t index: range(min(first,second)-Quarter, first-Quarter)) {
					float Y = (stemUp ? 1 : -1) * float(index) * (beamWidth+1);
					vec2 p0 (x[0]-stemWidth/2, tip[0]-beamWidth/2 + Y);
					vec2 p1 (x[1]+stemWidth/2, (tip[0]+tip[1])/2-beamWidth/2 + Y);
					p1 = (float(sign[1].duration)*p0 + float(sign[0].duration)*p1)/float(sign[0].duration+sign[1].duration);
					system.parallelograms.append(p0, p1, beamWidth, black, opacity);
				}
				for(size_t index: range(int(min(first,second)-Quarter), int(second-Quarter))) {
					float Y = (stemUp ? 1 : -1) * float(index) * (beamWidth+1);
					vec2 p0 (x[0]-stemWidth/2, tip[0]-beamWidth/2 + Y);
					vec2 p1 (x[1]+stemWidth/2, tip[1]-beamWidth/2 + Y);
					p0 = (float(sign[1].duration)*p0 + float(sign[0].duration)*p1)/float(sign[0].duration+sign[1].duration);
					system.parallelograms.append(p0, p1, beamWidth, black, opacity);
				}
			} else { // Draws grouping beam
				float firstStemY = Y(stemUp?beam.first().last():beam.first().first())+(stemUp?-1:1)*stemLength;
				float lastStemY = Y(stemUp?beam.last().last():beam.last().first())+(stemUp?-1:1)*stemLength;
				for(const Chord& chord: beam) {
					firstStemY = stemUp ? min(firstStemY, Y(chord.last())-shortStemLength) : max(firstStemY, Y(chord.first())+shortStemLength);
					lastStemY = stemUp ? min(lastStemY, Y(chord.last())-shortStemLength) : max(lastStemY, Y(chord.first())+shortStemLength);
				}
				array<float> stemsY;
				for(size_t index: range(beam.size)) { // Stems
					const Chord& chord = beam[index];
					float x = stemX(chord, stemUp);
					Sign sign = stemUp ? chord.first() : chord.last();
					float y = Y(sign) + (stemUp ? -1./2 : +1./2);
					float opacity = isTied(chord) ? 1./2 : 1;
					float stemY = firstStemY + (lastStemY-firstStemY) * (x - stemX(beam[0], stemUp))
							/ (stemX(beam.last(), stemUp) - stemX(beam[0], stemUp));
					stemsY.append(stemY);
					system.lines.append(vec2(x, ::min(y, stemY)), vec2(x, ::max(stemY, y)), black, opacity);
				}
				// Beam
				for(size_t chordIndex: range(beam.size-1)) {
					const Chord& chord = beam[chordIndex];
					Value value = chord[0].note.value;
					for(size_t index: range(value-Quarter)) {
						float dy = (stemUp ? 1 : -1) * float(index) * (beamWidth+1) - beamWidth/2;
						system.parallelograms.append(
									vec2(stemX(beam[chordIndex], stemUp)-(chordIndex==0?1./2:0), stemsY[chordIndex]+dy),
									vec2(stemX(beam[chordIndex+1], stemUp)+(chordIndex==beam.size-1?1./2:0), stemsY[chordIndex+1]+dy), beamWidth);
					}
				}
			}

			// Beams
			for(const Chord& chord: beam) {
				Sign sign = stemUp ? chord.first() : chord.last();
				float x = X(sign) + noteSize(sign)/2;
				int step = clefStep(sign);
				int y = Y(sign) + (stemUp?1:-1) * (lineInterval+(step%2?0:halfLineInterval));
				int base = SMuFL::Articulation::Base + stemUp;
				if(chord.first().note.accent) {
					int code = base+SMuFL::Articulation::Accent*2; glyph(vec2(x-glyphSize(code).x/2,y), code, font, system.glyphs); y += lineInterval;
				}
				if(chord.first().note.staccato) {
					int code = base+SMuFL::Articulation::Staccato*2; glyph(vec2(x-glyphSize(code).x/2,y), code, font, system.glyphs); y += lineInterval;
				}
				if(chord.first().note.tenuto) {
					int code = base+SMuFL::Articulation::Tenuto*2; glyph(vec2(x-glyphSize(code).x/2,y), code, font, system.glyphs); y += lineInterval;
				}
				//if(chord.first().note.trill) { glyph(vec2(x,y),"scripts.trill"_, font, system.glyphs); y+=lineInterval; }
				/*int y2 = staffY(staff, stemUp ? -10 : 2);
				y = stemUp ? max(y,y2) : min(y,y2);
				y -= (stemUp?0:glyphSize("scripts.sforzato"_).y/2);
				if(chord.first().note.accent) { glyph(vec2(x,y),"scripts.sforzato"_, font, system.glyphs); y+=lineInterval; }*/
			}
			beam.clear();
		};

		// Layouts justified line
		float spaceWidth = space + (pageSize ? float(pageSize.x-2*margin - allocatedLineWidth)/float(spaceCount) : 0);

		// Raster
		for(int staff: range(staffCount)) {
			for(int line: range(5)) {
				int y = staffY(staff, -line*2);
				system.lines.append(vec2(margin, y), vec2((pageSize?pageSize.x:allocatedLineWidth)-margin, y));
			}
		}

		// System first measure bar line
		{float x = margin;
			vec2 min(x, staffY(1,0)), max(x, staffY(0,-8));
			//system.fills.append(min, max-min);
			system.lines.append(min, max);
			measureBars[signs[startIndex].time] = x;
		}

		timeTrack.clear();
		{float x = margin + spaceWidth; timeTrack.insert(signs[startIndex].time, {{x,x},x,x,x,x});}

		for(size_t signIndex: range(startIndex, breakIndex)) {
			Sign sign = signs[signIndex];
			uint staff = sign.staff;

			auto doLedger = [this,&X, &noteSize, spaceWidth,&system](Sign sign) {// Ledger lines
				uint staff = sign.staff;
				const float x = X(sign);
				float ledgerLength = noteSize(sign)+min(noteSize(sign), spaceWidth/2);
				int step = clefStep(sign);
				for(int s=2; s<=step; s+=2) {
					int y=staffY(staff, s);
					system.lines.append(vec2(x+noteSize(sign)/2-ledgerLength/2,y),vec2(x+noteSize(sign)/2+ledgerLength/2,y));
				}
				for(int s=-10; s>=step; s-=2) {
					int y=staffY(staff, s);
					system.lines.append(vec2(x+noteSize(sign)/2-ledgerLength/2,y),vec2(x+noteSize(sign)/2+ledgerLength/2,y));
				}
			};

			if(staff < 2) { // Staff signs
				float x = X(sign);

				/**/ if(sign.type == Sign::OctaveShift) {
					/**/  if(sign.octave == Down) {
						x += text(vec2(x, staffY(1, max(0,currentLineHighestStep+7))), "8"+superscript("va"), textSize, system.glyphs);
						clefs[staff].octave++;
						timeTrack.at(sign.time).octave = x;
					} else if(sign.octave == Up) {
						x += text(vec2(x, staffY(1, max(0,currentLineHighestStep+7))), "8"+superscript("vb"), textSize, system.glyphs);
						clefs[staff].octave--;
						timeTrack.at(sign.time).octave = x;
					}
					else if(sign.octave == OctaveStop) {
						assert_(octaveStart[staff].octave==Down || octaveStart[staff].octave==Up, int(octaveStart[staff].octave));
						clefs[staff].octave--;
						float start = X(octaveStart[staff]) + space;
						float end = x;
						for(float x=start; x<end-space; x+=space*2) {
							//system.fills.append(vec2(x,staffY(1, 12)),vec2(space,1));
							system.lines.append(vec2(x,staffY(1, 12)),vec2(x+space,staffY(1, 12)+1));
						}
					}
					else error(int(sign.octave));
					octaveStart[staff] = sign;
				}
				else {
					if(sign.type == Sign::Note) {
						Note& note = sign.note;
						note.clef = clefs[staff];

						assert_(note.tie != Note::Merged);
						if(!note.grace) {
							x += glyphAdvance(SMuFL::NoteHead::Black);
							x += spaceWidth;
							for(Sign sign: chords[staff]) // Dichord
								if(abs(sign.note.step-note.step) <= 1) { x += glyphAdvance(SMuFL::NoteHead::Black); break; }
							chords[staff].insertSorted(sign);
						} else { // Grace  note
							error("grace");
							/*String noteGlyphName = "noteheads.s"_+str(clip(0,int(note.value-1),2)); // FIXME: factor
							vec2 noteSize = glyphSize(noteGlyphName, &smallFont);
							const float x = X(sign) - noteSize.x - glyphSize("flags.u3"_, &smallFont).x, y = Y(sign);

							doLedger(sign);
							// Body
							note.glyphIndex = system.glyphs.size;
							glyph(vec2(x, y), noteGlyphName, note.grace?smallFont:font, system.glyphs);
							assert_(!note.accidental);
							// Stem
							float stemX = x+noteSize.x-1./2;
							system.lines.append(vec2(stemX, y-shortStemLength), vec2(stemX, y-1./2));
							// Flag
							glyph(vec2(stemX, y-shortStemLength), "flags.u3"_, smallFont, system.glyphs);
							// Slash
							float slashY = y-shortStemLength/2;
							if(note.slash) FIXME Use SMuFL combining slash
										vec2(stemX +lineInterval/2, slashY -lineInterval/2),
										vec2(stemX -lineInterval/2, slashY +lineInterval/2));*/
						}
					}
					else if(sign.type == Sign::Rest) {
						doBeam(staff);
						vec2 p = vec2(x, staffY(staff, -4));
						x += glyph(p, SMuFL::Rest::Whole+int(sign.rest.value), smallFont, system.glyphs, 1./2);
						x += spaceWidth;
						uint measureLength = timeSignature.beats*quarterDuration;
						beatTime[staff] += sign.rest.value == Whole ? measureLength /*FIXME: only if single*/ : sign.rest.duration();
					}
					else if(sign.type == Sign::Clef) {
						Clef clef = sign.clef;
						if(clefs[staff].clefSign != sign.clef.clefSign || clefs[staff].octave != sign.clef.octave) {
							assert_(clefs[staff].clefSign != sign.clef.clefSign || clefs[staff].octave != sign.clef.octave);
							float y = staffY(staff, clef.clefSign==GClef ? -6 : -2);
							ClefSign clefSign = clef.clefSign;
							if(clef.octave==1) clefSign = ClefSign(clefSign+SMuFL::Clef::_8va);
							else assert_(clef.octave==0);
							x += glyph(vec2(x, y), clefSign, font, system.glyphs);
							x += space;
						}
						clefs[staff] = sign.clef;
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
				// Clearing signs (across staves)
				if(sign.type == Sign::Measure || sign.type==Sign::KeySignature || sign.type==Sign::TimeSignature || sign.type==Sign::Repeat) {
					assert_(timeTrack.contains(sign.time));
					float x = timeTrack.at(sign.time).maximum();

					if(sign.type==Sign::TimeSignature) {
						for(size_t staff: range(staffCount)) beatTime[staff] = 0;

						timeSignature = sign.timeSignature;
						String beats = str(timeSignature.beats);
						String beatUnit = str(timeSignature.beatUnit);
						float w = glyphAdvance(SMuFL::TimeSignature::_0);
						float W = max(beats.size, beatUnit.size)*w;
						float startX = x;
						x = startX + (W-beats.size*w)/2;
						for(char digit: beats) {
							glyph(vec2(x, staffY(0, -2)), SMuFL::TimeSignature::_0+digit-'0', font, system.glyphs);
							x += glyph(vec2(x, staffY(1, -2)),SMuFL::TimeSignature::_0+digit-'0', font, system.glyphs);
						}
						float maxX = x;
						x = startX + (W-beatUnit.size*w)/2;
						for(char digit: beatUnit) {
							glyph(vec2(x, staffY(0, -6)), SMuFL::TimeSignature::_0+digit-'0', font, system.glyphs);
							x += glyph(vec2(x, staffY(1, -6)), SMuFL::TimeSignature::_0+digit-'0', font, system.glyphs);
						}
						maxX = startX+2*glyphAdvance(SMuFL::TimeSignature::_0);
						timeTrack.at(sign.time).setStaves(maxX); // Does not clear directions lines
					} else { // Clears all lines (including direction lines)
						if(sign.type == Sign::Measure) {
							for(uint staff: range(staffCount)) doBeam(staff);

							system.lines.append(vec2(x, staffY(1,0)), vec2(x, staffY(0,-8)));

							//measureIndex = sign.measure.measure;
							//page = sign.measure.page;
							//lineIndex = sign.measure.pageLine;
							//lineMeasureIndex = sign.measure.lineMeasure;

							measureBars.insert(sign.time, x);
							x += spaceWidth;
							/*text(vec2(x, staffY(1, 12)), str(page)+','+str(lineIndex)+','+str(lineMeasureIndex)+' '+str(measureIndex), textSize,
								 debug->glyphs);*/
						}
						else if(sign.type==Sign::KeySignature) {
							keySignature = sign.keySignature;
							int fifths = keySignature.fifths;
							for(int i: range(abs(fifths))) {
								int step = fifths<0 ? 4+(3*i+2)%7 : 6+(4*i+4)%7;
								auto symbol = fifths<0 ? Accidental::Flat : Accidental::Sharp;
								glyph(vec2(x, Y(0, clefs[0].clefSign, step - (clefs[0].clefSign==FClef ? 14 : 0))), symbol, font, system.glyphs);
								x += glyph(vec2(x, Y(1, clefs[1].clefSign, step - (clefs[1].clefSign==FClef ? 14 : 0))), symbol, font, system.glyphs);
							}
							x += spaceWidth;
						}
						else if(sign.type==Sign::Repeat) {
							if(int(sign.repeat)>0) { // Ending
								float x = measureBars.values.last();
								/*- Text(str(int(sign.repeat)), textSize, 0, 1, 0, textFont).sizeHint(0).x/2*/
								text(vec2(x, staffY(1, max(0, currentLineHighestStep))-textSize),
									 str(int(sign.repeat)), textSize, system.glyphs);
							} else {
								float dotX = (sign.repeat==Repeat::Begin ? measureBars.values.last()+spaceWidth/2 : x-spaceWidth/2)
										- glyphSize(SMuFL::Dot).x/2;
								for(uint staff: range(staffCount)) {
									glyph(vec2(dotX, staffY(staff,-5)), SMuFL::Dot, font, system.glyphs);
									glyph(vec2(dotX, staffY(staff,-3)), SMuFL::Dot, font, system.glyphs);
								}
							}
						}
						else error(int(sign.type));
						timeTrack.at(sign.time).setAll(x);
					}
				} else { // Directions signs
					float& x = X(sign);
					if(sign.type == Sign::Metronome) {
						if(ticksPerMinutes!=int64(sign.metronome.perMinute*ticksPerQuarter)) {
							x += text(vec2(x, staffY(1, 12)), "♩="_+str(sign.metronome.perMinute)+" "_, textSize, system.glyphs);
							if(ticksPerMinutes) log(ticksPerMinutes, "->", int64(sign.metronome.perMinute*ticksPerQuarter)); // FIXME: variable tempo
							ticksPerMinutes = max(ticksPerMinutes, int64(sign.metronome.perMinute*ticksPerQuarter));
						}
					}
					else if(sign.type == Sign::Dynamic) {
						assert_(sign.dynamic!=-1);
						x += glyph(vec2(x, (staffY(1, -8)+staffY(0, 0))/2), sign.dynamic, font, system.glyphs);
					} else if(sign.type == Sign::Wedge) {
						int y = (staffY(1, -8)+staffY(0, 0))/2;
						if(sign.wedge == WedgeStop) {
							bool crescendo = wedgeStart.wedge == Crescendo;
							system.parallelograms.append( vec2(X(wedgeStart), y+(-!crescendo-1)*3), vec2(x, y+(-crescendo-1)*3), 1.f);
							system.parallelograms.append( vec2(X(wedgeStart), y+(!crescendo-1)*3), vec2(x, y+(crescendo-1)*3), 1.f);
						} else wedgeStart = sign;
					} else if(sign.type == Sign::Pedal) {
						int y = staffY(0, -20);
						if(sign.pedal == Ped) glyph(vec2(x, y), SMuFL::Pedal::Mark, font, system.glyphs);
						if(sign.pedal == Start) pedalStart = x + glyphAdvance(SMuFL::Pedal::Mark);
						if(sign.pedal == Change || sign.pedal == PedalStop) {
							system.lines.append(vec2(pedalStart, y), vec2(x, y));
							if(sign.pedal == PedalStop) {
								system.lines.append(vec2(x, y-lineInterval), vec2(x, y));
							} else {
								system.lines.append(vec2(x, y), vec2(x+space/2, y-space));
								system.lines.append(vec2(x+space/2, y-space), vec2(x+space, y));
								pedalStart = x + space;
							}
						}
					} else error(int(sign.type));
				}
			}

			for(uint staff: range(staffCount)) { // Layout notes, stems, beams and ties
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

					uint maximumBeamDuration = 2*quarterDuration; // why 2x ? // *4/timeSignature.beatUnit;

					if(beam &&
							( beamDuration > maximumBeamDuration /*Beam before too long*/
							  //|| beatTime[staff]%beatDuration==0 /*"Beam" before spanning (single chord)*/
							  //|| beam[0][0].note.tuplet != chord[0].note.tuplet
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
						const float x = X(sign) + (shift[index] ? noteSize(sign) : 0), y = Y(sign);
						float opacity = (note.tie == Note::NoTie || note.tie == Note::TieStart) ? 1 : 1./2;

						doLedger(sign);
						// Body
						note.pageIndex = pages.size;
						note.measureIndex = measureBars.size();
						note.glyphIndex = system.glyphs.size;
						glyph(vec2(x, y), noteCode(sign), note.grace?smallFont:font, system.glyphs, opacity);
						// Accidental
						if(note.accidental) {
							if(abs(y-accidentalAboveY)<=3*halfLineInterval) accidentalOffset -= space/2;
							else accidentalOffset = 0;
							glyph(vec2(x-glyphSize(note.accidental).x+accidentalOffset, y), note.accidental, font, system.glyphs); //TODO: highlight as well
							accidentalAboveY = y;
						}
						if(note.tie == Note::NoTie || note.tie == Note::TieStart) notes.sorted(sign.time).append( sign );
					}
					for(size_t index: reverse_range(chord.size)) {
						Sign sign = chord[index];
						assert_(sign.type==Sign::Note);
						Note& note = sign.note;
						String noteGlyphName = "noteheads.s"_+str(clip(0,int(note.value-1),2)); // FIXME: factor

						if(sign.note.tie == Note::TieContinue || sign.note.tie == Note::TieStop) {
							size_t tieStart = invalid;
							for(size_t index: range(activeTies[staff].size)) {
								if(activeTies[staff][index].key == note.key) {
									//assert_(tieStart==invalid, note.key, apply(activeTies[staff],[](TieStart o){return o.key;}));
									tieStart = index;
								}
							}
							if(tieStart != invalid) {
								assert_(tieStart != invalid, note.key, apply(activeTies[staff],[](TieStart o){return  o.key;}), signIndex);
								TieStart tie = activeTies[staff].take(tieStart);

								assert_(tie.position.y == P(sign).y);
								float y = P(sign).y;
								int slurDown = (chord.size>1 && index==chord.size-1) ? -1 :(y > staffY(staff, -4) ? 1 : -1);
								vec2 p0 = vec2(tie.position.x + noteSize(sign)/2 + (note.dot?space/2:0), y + slurDown*halfLineInterval);
								float bodyOffset = shift[index] ? noteSize(sign) : 0; // Shift body for dichords
								vec2 p1 = vec2(P(sign).x + bodyOffset + noteSize(sign)/2, y + slurDown*halfLineInterval);
								if(P(sign).x > tie.position.x) {
									const float offset = min(halfLineInterval, (P(sign).x-tie.position.x)/4), width = halfLineInterval/2;
									vec2 k0 (p0.x, p0.y + slurDown*offset);
									vec2 k0p (k0.x, k0.y + slurDown*width);
									vec2 k1 (p1.x, p1.y + slurDown*offset);
									vec2 k1p (k1.x, k1.y + slurDown*width);
									system.cubics.append(copyRef(ref<vec2>({p0,k0,k1,p1,k1p,k0p})), black, 1.f/2);
								} else { // Wrapped tie
									// TODO: Tie start
									// Tie end
									p0.x = p1.x - 2*lineInterval;
									const float offset = halfLineInterval, width = halfLineInterval/2;
									vec2 k0 (p0.x, p0.y + slurDown*offset);
									vec2 k0p (k0.x, k0.y + slurDown*width);
									vec2 k1 (p1.x, p1.y + slurDown*offset);
									vec2 k1p (k1.x, k1.y + slurDown*width);
									system.cubics.append(copyRef(ref<vec2>({p0,k0,k1,p1,k1p,k0p})), black, 1.f/2);
								}
							} else sign.note.tie = Note::NoTie;
						}
						if(sign.note.tie == Note::TieStart || sign.note.tie == Note::TieContinue) {
							float bodyOffset = shift[index] ? noteSize(sign) : 0; // Shift body for dichords
							activeTies[staff].append(note.key, P(sign)+vec2(bodyOffset, 0));
						}

						note.step = note.step/2*2 +1; // Aligns dots between lines (WARNING: moves note in this scope)
						if(note.dot) {
							float opacity = (note.tie == Note::NoTie || note.tie == Note::TieStart) ? 1 : 1./2;
							float dotOffset = glyphSize(SMuFL::NoteHead::Black).x*7/6;
							glyph(P(sign)+vec2((shift.contains(true) ? noteSize(sign) : 0)+dotOffset,0), SMuFL::Dot, font, system.glyphs, opacity);
						}
					}
					if(value >= Half /*also quarter and halfs for stems*/) {
						if(!beam) beamStart[staff] = beatTime[staff];
						if(value <= Quarter) doBeam(staff); // Flushes any pending beams
						beam.append(copy(chord));
						if(value <= Quarter) doBeam(staff); // Only stems for quarter and halfs (actually no beams :/)
					}
					uint tupletSize = chord[0].note.tuplet;
					auto& tuplet = tuplets[staff];
					if(tupletSize) {	// Tuplet
						tuplet.append(copy(chord));

						uint totalDuration = 0;
						for(const auto& chord: tuplet) {
							assert_(chord);
							uint duration = chord[0].note.duration();
							for(Sign sign: chord) {
								assert_(sign.note.tuplet == tupletSize);
								duration = min(duration, sign.note.duration());
							}
							totalDuration += duration;
						}
						if(tuplet.size == tupletSize) {
							int sum = 0, count=0;
							for(Chord& chord: tuplet) {
								for(Sign& sign: chord) sum += clefStep(sign);
								count += chord.size;
							}
							bool stemUp = sum < -4*count; // sum/count<-4 (Average note height below mid staff)

							array<float> stemXs;
							for(const Chord& chord: tuplet) { // Stems X
								String noteGlyphName = "noteheads.s"_+str(clip(0,int(chord[0].note.value-1),2)); // FIXME: factor, FIXME: assumes same width
								stemXs.append(stemX(chord, stemUp));
							}
							float x = (stemXs.first() + stemXs.last()) / 2;

							float firstStemY = Y(stemUp?tuplet.first().last():tuplet.first().first())+(stemUp?-1:1)*stemLength;
							float lastStemY = Y(stemUp?tuplet.last().last():tuplet.last().first())+(stemUp?-1:1)*stemLength;
							float stemY = (firstStemY + lastStemY) / 2;
							float dy = (stemUp ? -1 : 1) * 2 * beamWidth;
							float y = stemY+dy;
							vec2 size(Text(str(tupletSize),textSize/2,0,1,0,textFont).sizeHint(0));
							text(vec2(x,y) - size/2.f, str(tupletSize), textSize/2, system.glyphs);
							if(totalDuration > quarterDuration) { // No beam ? draw lines
								system.lines.append(
											vec2(stemXs.first(),firstStemY+dy),
											vec2(x-size.x, firstStemY+((x-size.x)-stemXs.first())/(stemXs.last()-stemXs.first())*(lastStemY-firstStemY)+dy), black);
								system.lines.append(
											vec2(x+size.x,firstStemY+((x+size.x)-stemXs.first())/(stemXs.last()-stemXs.first())*(lastStemY-firstStemY)+dy),
											vec2(stemXs.last(),lastStemY+dy), black);
							}
							tuplet.clear();
						}
					} else {
						tuplet.clear(); // FIXME
						assert_(!tuplet, tuplet);
					}
					chord.clear();
					beatTime[staff] += duration;
				}
			}
		}
		//lineBreaks.append(measures.size());
		lineCount++; //pageLineCount++;
		system.translate(vec2(0,offsetY));
		systems.append(move(system));
		offsetY += staffY(0, min(lowMargin, currentLineLowestStep)-7);
		startIndex = breakIndex;
	}
	doPage();
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
				if(note.pageIndex != invalid && note.glyphIndex != invalid) {
					vec2 p = pages[note.pageIndex].glyphs[note.glyphIndex].origin;
					text(p+vec2(space, 2), "O"_+str(note.key), 12, debug->glyphs);
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
			if(note.pageIndex != invalid && note.glyphIndex != invalid) {
				vec2 p = pages[note.pageIndex].glyphs[note.glyphIndex].origin;
				text(p+vec2(space, 2), str(note.key), 12, debug->glyphs);
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
					if(note.pageIndex != invalid && note.glyphIndex != invalid) {
						vec2 p = pages[note.pageIndex].glyphs[note.glyphIndex].origin;
						text(p+vec2(space, 2), str(note.key), 12, debug->glyphs);
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
					if(note.pageIndex != invalid && note.glyphIndex != invalid) {
						vec2 p = pages[note.pageIndex].glyphs[note.glyphIndex].origin;
						text(p+vec2(space, 2), str(note.key)+"?"_+str(midiKey)+"!"_, 12, debug->glyphs);
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
}

inline bool operator ==(const Sign& sign, const uint& key) {
	assert_(sign.type == Sign::Note);
	return sign.note.key == key;
}

shared<Graphics> Sheet::graphics(int2, Rect) { return shared<Graphics>(&pages[pageIndex]); }

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
	if(key==RightArrow) { pageIndex = min(pageIndex+1, pages.size-1); return true; }
	return false;
}
