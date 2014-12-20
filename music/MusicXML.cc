#include "MusicXML.h"
#include "xml.h"

//generic uint argmin(const ref<T>& a) { uint min=0; for(uint i: range(a.size)) if(a[i] < a[min]) min=i; return min; }
generic uint argmax(const ref<T>& a) { uint max=0; for(uint i: range(a.size)) if(a[i] > a[max]) max=i; return max; }

static int implicitAlteration(int keySignature, const map<int, int>& measureAlterations, int step) {
	int implicitAlteration = 0;

	// Implicit alteration from key signature
	int octaveStep = (step - step/7*7 + 7)%7; // signed step%7 (Step offset on octave scale)
	for(int i: range(abs(keySignature))) {
		int fifthStep = keySignature<0 ? 4+(3*i+2)%7 : 6+(4*i+4)%7;
		if(octaveStep == fifthStep%7) implicitAlteration = keySignature>0 ? 1 : -1;
	}

	// Implicit alteration from previous accidental in same measure
	if(measureAlterations.contains(step)) implicitAlteration = measureAlterations.at(step);

	return implicitAlteration;
}

MusicXML::MusicXML(string document, string) {
    Element root = parseXML(document);

	map<uint, uint> voiceToStaff;
	{
		map<uint, map<uint, uint>> staffsVoicesNoteCount;
		size_t partIndex = 0;
		for(const Element& p: root("score-partwise"_).children) {
			if(p.name!="part"_) continue;
			for(const Element& m: p.children) {
				for(const Element& e: m.children) {
					if(e.name=="note"_) {
						if(e["print-object"_]=="no"_) continue;
						uint xmlStaffIndex = e.contains("staff") ? parseInteger(e("staff"_).text())-1 : partIndex;
						uint voiceIndex = e.contains("voice") ? parseInteger(e("voice"_).text())-1 : xmlStaffIndex;
						staffsVoicesNoteCount[xmlStaffIndex][voiceIndex]++;
					}
				}
			}
			partIndex++;
		}
		//log(staffVoiceCounts);
		for(auto staffVoicesNoteCount: staffsVoicesNoteCount) {
			uint staff = staffVoicesNoteCount.key;
			const map<uint, uint>& voicesNotesCount = staffVoicesNoteCount.value;
			uint mostCommonVoice = voicesNotesCount.keys[argmax(voicesNotesCount.values)];
			assert_(!voiceToStaff.contains(mostCommonVoice)); // Each voice should be most common in an unique staff
			voiceToStaff[mostCommonVoice] = staff;
		}
	}

	const size_t staffCount = 2;
	Clef clefs[staffCount] = {{FClef,0}, {GClef,0}};
	for(uint staff: range(staffCount)) signs.insertSorted(Sign{0, 0, staff, Sign::Clef, .clef=clefs[staff]}); // Defaults
	size_t partIndex = 0;
	for(const Element& p: root("score-partwise"_).children) {
		if(p.name!="part"_) continue;

		KeySignature keySignature = 0; TimeSignature timeSignature={4,4};
		int64 measureTime = 0, time = 0, nextTime = 0, maxTime = 0;
		uint globalMeasureIndex=0, pageIndex=0, lineIndex=0, measureIndex=0; // starts with 1
		//size_t repeatIndex = invalid;
		array<size_t> activeTies;
		array<int> fingering;
		for(const Element& m: p.children) {

			auto insertSign = [this, &activeTies](Sign sign) {
				size_t signIndex = signs.insertSorted(sign);
				for(size_t& index: activeTies) if(signIndex <= index) index++;
				return signIndex;
			};

			measureTime = time;
			globalMeasureIndex++; measureIndex++;
			assert_(m.name=="measure"_, m);
			map<int, int> measureAlterations; // Currently altered steps (for implicit alterations)
			array<Sign> acciaccaturas; // Acciaccatura graces for pending principal
			int appoggiaturaTime = 0; // Appoggiatura time to remove from pending principal
			for(const Element& e: m.children) {
				if(!(e.name=="note"_ && e.contains("chord"_))) time = nextTime; // Advances time (except chords)
				maxTime = max(maxTime, time);

				if(e.name=="note"_) {
					Value value = e.contains("type"_) ? Value(ref<string>(valueNames).indexOf(e("type"_).text())) : Whole;
					assert_(int(value)!=-1);
					int duration;
					if(e.contains("grace"_)) {
						//assert_(uint(value)<Sixteenth && divisions%quarterDuration == 0, int(value), divisions);
						duration = valueDurations[uint(value)]*divisions/quarterDuration;
					} else {
						int acciaccaturaTime = 0;
						for(Sign grace: acciaccaturas.reverse()) { // Inserts any pending acciaccatura graces before principal
							acciaccaturaTime += grace.duration;
							grace.time = time; //FIXME: -acciaccaturaTime;
							insertSign(grace);
						}
						duration = parseInteger(e("duration"_).text());
						bool dot = e.contains("dot"_) ? true : false;
						/*if(int(value)==-1) {
						uint valueDuration = duration*quarterDuration/divisions;
						if(valueDuration%3 == 0) {
							dot = true;
							valueDuration = valueDuration * 2 / 3;
						}
						value = Value(ref<uint>(valueDurations).size-1-log2(valueDuration));
						assert_(int(value)>=0, duration, divisions, timeSignature.beats, timeSignature.beatUnit,e);
					}*/
						assert_(int(value)!=-1);
						assert_(uint(value) < ref<uint>(valueDurations).size, int(value), e);
						uint notationDuration = valueDurations[uint(value)]*divisions/quarterDuration;
						if(e.contains("rest"_) && value==Whole) notationDuration = timeSignature.beats*divisions;
						if(dot) notationDuration = notationDuration * 3 / 2;
						if(e.contains("time-modification"_)) {
							notationDuration = notationDuration * parseInteger(e("time-modification"_)("normal-notes").text())
									/ parseInteger(e("time-modification"_)("actual-notes").text());
						}
						duration -= min(duration, appoggiaturaTime); //FIXME
						assert_(acciaccaturaTime <= duration, acciaccaturaTime, duration, appoggiaturaTime);
						acciaccaturas.clear();
						appoggiaturaTime = 0;
					}
					assert_(duration >= 0, duration);
					if(!e.contains("chord"_) && (!e.contains("grace"_) || e("grace"_)["slash"_]!="yes"_)) nextTime = time+duration;
					if(e["print-object"_]=="no"_) continue;
					//assert_(e.contains("staff"), e);
					uint xmlStaffIndex = e.contains("staff") ? parseInteger(e("staff"_).text())-1 : partIndex;
					if(e.contains("voice")) {
						uint voiceIndex = parseInteger(e("voice"_).text())-1;
						if(!voiceToStaff.contains(voiceIndex) && e.contains("rest"_)) continue;
						if(voiceToStaff.contains(voiceIndex)) {
							assert_(voiceToStaff.contains(voiceIndex), e, pageIndex, lineIndex, measureIndex);
							uint voiceStaffIndex = voiceToStaff.at(voiceIndex);
							if(voiceStaffIndex != xmlStaffIndex) {
								log(xmlStaffIndex, voiceStaffIndex, voiceIndex);
								assert_(voiceStaffIndex < staffCount);
								xmlStaffIndex = voiceStaffIndex;
							}
						}
					}
					uint staff = 1-xmlStaffIndex; // Inverts staff order convention: (top/treble, bottom/bass) -> (bottom/bass, top/treble)
					assert_(staff < staffCount, staff);
					assert_(int(value)>=0, e);
					if(e.contains("rest"_)) insertSign({time, duration, staff, Sign::Rest, .rest={value}});
					else {
						assert_(e("pitch"_)("step"_).text(), e);
						int xmlOctaveStep = "CDEFGAB"_.indexOf(e("pitch"_)("step"_).text()[0]);
						assert_(xmlOctaveStep >= 0);
						int noteOctave = parseInteger(e("pitch"_)("octave"_).text());
						const int step = (noteOctave-4) * 7 + xmlOctaveStep;
						int octaveStep = (step - step/7*7 + 7)%7; // signed step%7 (Step offset on octave scale)
						assert_(xmlOctaveStep == octaveStep);
						int xmlAlteration = e("pitch"_).contains("alter"_) ? parseInteger(e("pitch"_)("alter"_).text()) : 0;
						assert_(xmlAlteration >= -1 && xmlAlteration <= 1, xmlAlteration);

						// Tie
						Note::Tie tie = Note::NoTie;
						auto tieLambda = [&](const Element& e){
							/**/ if((e["type"_] == "start"_ && tie == Note::TieStop) ||
									(e["type"_] == "stop"_ && tie == Note::TieStart)) tie = Note::TieContinue;
							else if(e["type"_] == "start"_) tie = Note::TieStart;
							else if(e["type"_] == "stop"_) tie = Note::TieStop;
							else error("");
						};
						e.xpath("tie"_, tieLambda);
						e.xpath("notations/tied"_, tieLambda);

						// -- -- Accidental
						// -- Implicit alteration
						int implicitAlteration = 0;

						// Implicit accidental from key signature
						for(int i: range(abs(keySignature))) {
							int fifthStep = keySignature<0 ? 4+(3*i+2)%7 : 6+(4*i+4)%7;
							if(octaveStep == fifthStep%7) implicitAlteration = keySignature>0 ? 1 : -1;
						}

						// Implicit accidental from previous accidental in same measure
						if(measureAlterations.contains(step)) implicitAlteration = measureAlterations.at(step);

						// Implicit accidental from tie start note
						if(tie == Note::TieContinue || tie == Note::TieStop) {
							size_t tieStart = invalid;
							for(size_t index: range(activeTies.size)) if(signs[activeTies[index]].note.step == step) {
								assert_(tieStart==invalid); tieStart = index;
							}
							assert_(tieStart != invalid, e, pageIndex, lineIndex, measureIndex,
									apply(activeTies, [&](size_t index){ return signs[index];}),
									apply(activeTies, [&](size_t index){ return signs[index].note.step;}), step);
							size_t tieStartNoteIndex = activeTies[tieStart];
							implicitAlteration = signs[tieStartNoteIndex].note.alteration;
							if(tie == Note::TieStop) activeTies.removeAt(tieStart);
						}

						// -- Accidental
						Accidental accidental = e.contains("accidental"_) ?
									Accidental(SMuFL::AccidentalBase + ref<string>(SMuFL::accidental).indexOf(e("accidental"_).text())) : Accidental::None;
						assert_(accidental != -1, e("accidental"_));

						int alteration = accidental ? accidentalAlteration(accidental): implicitAlteration;
						if(xmlAlteration != alteration) {
							log("Alteration mismatch", alteration, xmlAlteration);
							accidental = alterationAccidental(xmlAlteration);
							alteration = xmlAlteration;
						}

						// Redundant accidental
						//if(accidental == implicitAccidental) accidental = Accidental::None;

						// Records alteration used for the measure
						if(accidental) measureAlterations[step] = alteration;

						// Converts note (step, accidental) to MIDI key
						int octave = clefs[staff].octave + step>0 ? step/7 : (step-6)/7; // Rounds towards
						uint octaveStepToKey[] = {0,2,4,5,7,9,11}; // C [C#] D [D#] E F [F#] G [G#] A [A#] B
						uint key = 60 + octave*12 + octaveStepToKey[octaveStep] + alteration;

						bool articulations = e.contains("notations"_) && e("notations"_).contains("articulations"_);
						bool ornaments = e.contains("notations"_) && e("notations"_).contains("ornaments"_);
						{
							Sign sign{
								time, duration, staff, Sign::Note, .note={
									.clef=clefs[staff],
									.step=step,
									.alteration=alteration,
									.accidental=accidental,
									.key = key,
									.value=value,
									.tie=tie,
									.tuplet = e.contains("time-modification"_) ? (uint)parseInteger(e("time-modification"_)("actual-notes").text()) : 0,
									.dot = e.contains("dot"_) ? true : false,
									.grace = e.contains("grace"_)?true:false,
									.acciaccatura = e.contains("grace"_) && e("grace"_)["slash"_]=="yes"_?true:false,
									.accent= articulations && e("notations"_)("articulations"_).contains("accent"_)?true:false,
									.staccato= articulations && e("notations"_)("articulations"_).contains("staccato"_)?true:false,
									.tenuto= articulations && e("notations"_)("articulations"_).contains("tenuto"_)?true:false,
									.trill=ornaments && e("notations"_)("ornaments"_).contains("trill-mark"_)?true:false,
									.finger = fingering ? fingering.take(0) : 0
									//.stem = e.contains("stem"_) && e("stem").text() == "up"_,
								}
							};
							// Acciaccatura are played before principal beat. Records graces to shift in on parsing principal
							if(sign.note.acciaccatura) acciaccaturas.append( sign ); // FIXME: display after measure bar
							else {
								if(sign.note.grace) appoggiaturaTime += duration; // Takes time away from principal (appoggiatura)
								size_t signIndex = insertSign( sign );
								if(tie == Note::TieStart) activeTies.append(signIndex);
							}
						}
					}
				}
				else if(e.name=="backup"_) {
					int dt = parseInteger(e("duration"_).text());
					time -= dt;
					nextTime = time;
				}
				else if(e.name=="forward"_) {
					int dt =  parseInteger(e("duration"_).text());
					time += dt;
					maxTime = max(maxTime, time);
					nextTime = time;
				}
				else if(e.name=="direction"_) {
					if(partIndex != 0) continue;
					for(const Element& d : e.children) {
						if(d.name !="direction-type"_) continue;
						if(d.contains("dynamics"_)) {
							Dynamic dynamic = Dynamic(SMuFL::DynamicBase + ref<string>(SMuFL::dynamic).indexOf(d("dynamics"_).children.first()->name));
							assert_(dynamic!=-1, d);
							insertSign({time, 0, uint(-1), Sign::Dynamic, .dynamic=dynamic});
						}
						else if(d.contains("metronome"_)) {
							Value beatUnit = Value(ref<string>({"whole"_,"half"_,"quarter"_,"eighth"_,"16th"_})
												   .indexOf(d("metronome"_)("beat-unit"_).text()));
							uint perMinute = parseInteger(d("metronome"_)("per-minute"_).text());
							insertSign({time, 0, uint(-1), Sign::Metronome, .metronome={beatUnit, perMinute}});
						}
						else if(d.contains("pedal"_)) {
							Pedal pedal = Pedal(ref<string>({"start"_,"change"_,"stop"_}).indexOf(d("pedal"_)["type"_]));
							if(pedal==Start && d("pedal"_)["line"_]!="yes"_) pedal=Ped;
							int offset = e.contains("offset"_) ? parseInteger(e("offset"_).text()) : 0;
							if((offset+1)%(divisions/2) == 0) offset++; // FIXME
							insertSign({time + offset, 0, uint(-1), Sign::Pedal, .pedal=pedal});
						}
						else if(d.contains("wedge"_)) {
							Wedge wedge = Wedge(ref<string>({"crescendo"_,"diminuendo"_,"stop"_}).indexOf(d("wedge"_)["type"_]));
							insertSign({time, 0, uint(-1), Sign::Wedge, .wedge=wedge});
						}
						else if(d.contains("octave-shift"_)) {
							OctaveShift octave = OctaveShift(ref<string>({"down"_,"up"_,"stop"_}).indexOf(d("octave-shift"_)["type"_]));
							insertSign({time, 0, 0/*FIXME*//*uint(-1)*/, Sign::OctaveShift, .octave=octave});
						}
						else if(d.contains("other-direction"_)) {}
						else if(d.contains("words"_)) {
							if(isInteger(d("words").text())) fingering.append(parseInteger(d("words").text())); // Fingering
							// else { TODO: directions }
						}
						else if(d.contains("rehearsal"_)) {}
						else if(d.contains("bracket"_)) {}
						else error(e);
					}
					if(e.contains("sound"_) && e("sound"_)["tempo"_]) {
						insertSign({time, 0, uint(-1), Sign::Metronome,
											.metronome={Quarter, uint(parseDecimal(e("sound"_).attribute("tempo"_)))}});
					}
				}
				else if(e.name=="attributes"_) {
					if(e.contains("divisions"_)) divisions = parseInteger(e("divisions"_).text());
					e.xpath("clef"_, [&](const Element& clef) {
						uint xmlStaffIndex = clef["number"_] ? parseInteger(clef["number"_])-1 : partIndex;
						uint staff = 1-xmlStaffIndex; // Inverts staff order convention (treble, bass -> bass, treble)
						assert_(staff >= 0 && staff <= 1);
						ClefSign clefSign = ref<ClefSign>{FClef,GClef}["FG"_.indexOf(clef("sign"_).text()[0])];
						int octave = 0;
						if(clef.contains("clef-octave-change")) octave = parseInteger(clef("clef-octave-change").text());
						insertSign({time, 0, staff, Sign::Clef, .clef={clefSign, octave}});
						clefs[staff] = {clefSign, 0};
					});
					if(e.contains("key"_)) {
						keySignature = parseInteger(e("key"_)("fifths"_).text());
						if(partIndex==0) insertSign({time, 0, uint(-1), Sign::KeySignature, .keySignature=keySignature});
					}
					if(e.contains("time"_)) {
						timeSignature = {uint(parseInteger(e("time"_)("beats"_).text())), uint(parseInteger(e("time"_)("beat-type"_).text()))};
						if(partIndex==0) insertSign({time, 0, uint(-1), Sign::TimeSignature, .timeSignature=timeSignature});
					}
				}
				else if(e.name=="print"_) {
					if(e["new-system"]=="yes") { lineIndex++, measureIndex=1; }
					if(e["new-page"]=="yes") { pageIndex++, lineIndex=1; }
				}
				else if(e.name=="barline"_) {
#if 0 // TODO: display
					if(e.contains("repeat")) {
						if(e("repeat")["direction"]=="forward") {
							assert_(repeatIndex==invalid);
							repeatIndex=signs.size;
						}
						else if(e("repeat")["direction"]=="backward") {
							assert_(partIndex == 0);
							if(repeatIndex==invalid) { /*signs.clear();*/ log("Unmatched repeat"); } // FIXME
							else {
								assert_(repeatIndex!=invalid);
								buffer<Sign> copy = copyRef(signs.slice(repeatIndex));
								assert_(time==nextTime && time==maxTime && time > signs[repeatIndex].time);
								int64 repeatLength = time - signs[repeatIndex].time; // FIXME: Assumes document order matches time order
								for(Sign& sign: copy) sign.time += repeatLength;
								signs.insertSorted({time, 0, uint(-1), Sign::Measure,
													.measure={false, globalMeasureIndex, pageIndex, lineIndex, measureIndex}});
								signs.append( move(copy) );
								nextTime = time + repeatLength;
							}
							repeatIndex=invalid;
						}
						else error(e);
					}
#else
					if(e.contains("repeat")) {
						if(e("repeat")["direction"]=="forward") {
							if(partIndex==0) insertSign({time, 0, uint(-1), Sign::Repeat, .repeat=Repeat::Begin});
						}
						else if(e("repeat")["direction"]=="backward") {
							if(partIndex==0) insertSign({time, 0, uint(-1), Sign::Repeat, .repeat=Repeat::End});
						}
						else error(e);
					}
					if(e.contains("ending") && e("ending")["type"]=="start") {
						if(partIndex==0) insertSign({time, 0, uint(-1), Sign::Repeat, .repeat=Repeat(parseInteger(e("ending")["number"]))});
					}
#endif
				}
				else if(e.name=="harmony"_) {}
				else error(e);

				assert_(time >= measureTime, int(time-measureTime), int(nextTime-measureTime), int(maxTime-measureTime), globalMeasureIndex, e);
			}
			maxTime=time=nextTime= max(maxTime, max(time, nextTime));
			Break measureBreak = NoBreak;
			if(m.contains("print")) {
				if(m("print")["new-page"]=="yes") measureBreak=PageBreak;
				else if(m("print")["new-system"]=="yes") measureBreak = LineBreak;
			}
			assert_(time > measureTime);
			if(partIndex == 0)
				insertSign({time, 0, uint(-1), Sign::Measure,
									.measure={measureBreak, globalMeasureIndex, pageIndex, lineIndex, measureIndex}});
		}
		partIndex++;
	}

	/*// Matches start-end-stop signs together
	for(size_t startIndex: range(signs.size)) {
		Sign& start = signs[startIndex];
		if(start.type == Sign::Slur && start.slur.type == SlurStart) {
			assert_(!start.slur.matched);
			for(size_t stopIndex: range(startIndex+1, signs.size)) {
				Sign& stop = signs[stopIndex];
				if(stop.type == Sign::Slur && stop.slur.type == SlurStop && !stop.slur.matched && start.slur.index==stop.slur.index
						&& (start.slur.index!=-1 || start.staff == stop.staff)) {
					start.staff=stop.staff= (start.staff==stop.staff ? start.staff : -1);
					start.slur.matched = true; stop.slur.matched=true;
					break;
				}
			}
		}
	}*/

#if 1
	// Removes unused clef change
	for(size_t signIndex=0; signIndex <signs.size;) {
		Sign& sign = signs[signIndex];
		if(sign.type==Sign::Clef) {
			for(size_t nextIndex: range(signIndex+1, signs.size)) {
				Sign& next = signs[nextIndex];
				if(next.type==Sign::Note) { signIndex++; break; }
				if(next.type==Sign::Clef && next.staff == sign.staff) { signs.removeAt(signIndex); break; }
			}
		}
		else signIndex++;
	}
#endif

#if 1
	{// Converts ties to longer notes (spanning beats and measures)
	array<size_t> activeTies;
	uint page=0, line=0, measure=0;
	for(size_t signIndex=0; signIndex < signs.size;) {
		Sign& sign = signs[signIndex];
		if(sign.type == Sign::Measure) { page=sign.measure.page, line=sign.measure.pageLine, measure=sign.measure.lineMeasure; }
		if(sign.type == Sign::Note && (sign.note.tie == Note::TieStart)) activeTies.append(signIndex);
		if(sign.type == Sign::Note && (sign.note.tie == Note::TieContinue || sign.note.tie == Note::TieStop)) {
			size_t tieStart = invalid;
			for(size_t index: range(activeTies.size)) {
				assert_((signs[activeTies[index]].note.step == sign.note.step) == (signs[activeTies[index]].note.key == sign.note.key),
						signs[activeTies[index]].note.step, sign.note.step,
						signs[activeTies[index]].note.key, sign.note.key, page, line, measure);
				if(signs[activeTies[index]].note.step == sign.note.step) { assert_(tieStart==invalid); tieStart = index; }
			}
			assert_(tieStart != invalid, page, line, measure, activeTies, apply(activeTies, [&](size_t index){ return signs[index];}), sign);
			size_t firstIndex = activeTies[tieStart];
			if(sign.note.tie == Note::TieStop) activeTies.removeAt(tieStart);
			Sign& first = signs[firstIndex];
			bool inBetween = false;
			// Only merges if no elements (notes, bars) would break the tie
			for(Sign sign: signs.slice(firstIndex, signIndex-firstIndex)) { if(sign.time > first.time) { inBetween = true; break; } }
			if(!inBetween) {
				first.duration = sign.time+sign.duration-first.time;
				int duration = first.note.duration() + sign.note.duration();
				bool dot = false;
				if(duration%3 == 0) {
					dot = true;
					duration = duration * 2 / 3;
				}
				if(isPowerOfTwo(duration)) {
					first.note.value = Value(ref<uint>(valueDurations).size-1-log2(duration));
					first.note.dot = dot;
					assert_(int(first.note.value)>=0);
					first.note.tie = Note::NoTie;
					signs.removeAt(signIndex);
					continue;
				}
			}
		}
		signIndex++;
	}
	}
#endif

#if 1
	{// Evaluates tie note index links (invalidated by any insertion/deletion)
	array<size_t> activeTies;
	uint page=0, line=0, measure=0;
	for(size_t signIndex=0; signIndex < signs.size; signIndex++) {
		Sign& sign = signs[signIndex];
		if(sign.type == Sign::Measure) { page=sign.measure.page, line=sign.measure.pageLine, measure=sign.measure.lineMeasure; }
		if(sign.type == Sign::Note && (sign.note.tie == Note::TieStart)) activeTies.append(signIndex);
		if(sign.type == Sign::Note && (sign.note.tie == Note::TieContinue || sign.note.tie == Note::TieStop)) {
			size_t tieStart = invalid;
			for(size_t index: range(activeTies.size)) {
				assert_((signs[activeTies[index]].note.step == sign.note.step) == (signs[activeTies[index]].note.key == sign.note.key));
				if(signs[activeTies[index]].note.step == sign.note.step) { assert_(tieStart==invalid); tieStart = index; }
			}
			assert_(tieStart != invalid, page, line, measure, activeTies, apply(activeTies, [&](size_t index){ return signs[index];}), sign);
			sign.note.tieStartNoteIndex = activeTies[tieStart];
			if(sign.note.tie == Note::TieStop) activeTies.removeAt(tieStart);
		}
	}
	}
#endif

#if 1 // Converts accidentals to match key signature (pitch class). Tie support needs explicit tiedNoteIndex to match ties while editing steps
	KeySignature keySignature = 0;
	map<int, int> measureAlterations; // Currently accidented steps (for implicit accidentals)
	for(Sign& sign: signs) {
		if(sign.type == Sign::Measure) measureAlterations.clear();
		if(sign.type == Sign::KeySignature) keySignature = sign.keySignature;
		if(sign.type == Sign::Note) {
			if(sign.note.tie == Note::TieContinue || sign.note.tie == Note::TieStop)  {
				assert_(sign.note.tieStartNoteIndex);
				assert_(signs[sign.note.tieStartNoteIndex].type == Sign::Note && (
							signs[sign.note.tieStartNoteIndex].note.tie == Note::TieStart
						|| signs[sign.note.tieStartNoteIndex].note.tie == Note::TieContinue), sign,
						sign.note.tieStartNoteIndex, signs[sign.note.tieStartNoteIndex]);
				sign.note.step = signs[sign.note.tieStartNoteIndex].note.step;
				sign.note.alteration = signs[sign.note.tieStartNoteIndex].note.alteration;
				assert_(!sign.note.accidental);
				continue;
			}
			// Recomputes accidental to reflect any previous changes in the same measure
			sign.note.accidental =
					(sign.note.alteration == implicitAlteration(keySignature, measureAlterations, sign.note.step) ? Accidental::None :
																													alterationAccidental(sign.note.alteration));

			int step = keyStep(sign.note.key, keySignature);
			int alteration = keyAlteration(sign.note.key, keySignature);

			Accidental accidental =
					(alteration == implicitAlteration(keySignature, measureAlterations, step) ? Accidental::None : alterationAccidental(alteration));
			if((accidental == sign.note.accidental) || // No operation
					(accidental && !sign.note.accidental) || // Does not introduce additional accidentals (for ambiguous tones)
					// Only switches toward key
					(accidental && sign.note.accidental && (accidental < sign.note.accidental) != (keySignature < 0))) {
				if(sign.note.accidental) measureAlterations[step] = sign.note.alteration;
				continue;
			}
			if(sign.note.accidental && measureAlterations.contains(sign.note.step)) // FIXME: restore previous
				measureAlterations.remove(sign.note.step);
			sign.note.step = step;
			sign.note.alteration = alteration;
			sign.note.accidental = accidental;
			if(accidental) measureAlterations[step] = alteration;
		}
	}
#endif

	assert_(signs);
}
