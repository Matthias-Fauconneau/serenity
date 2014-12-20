#include "MusicXML.h"
#include "xml.h"

//generic uint argmin(const ref<T>& a) { uint min=0; for(uint i: range(a.size)) if(a[i] < a[min]) min=i; return min; }
generic uint argmax(const ref<T>& a) { uint max=0; for(uint i: range(a.size)) if(a[i] > a[max]) max=i; return max; }

static int implicitAlteration(int keySignature, const map<int, int>& measureAlterations, int step) {
	return measureAlterations.contains(step) ? measureAlterations.at(step) : signatureAlteration(keySignature, step);
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
	Sign octaveStart[staffCount] {{.octave=OctaveStop}, {.octave=OctaveStop}}; // Current octave shift (for each staff)
	for(uint staff: range(staffCount)) signs.insertSorted({Sign::Clef, 0, {{staff, {.clef=clefs[staff]}}}}); // Defaults
	size_t partIndex = 0;
	for(const Element& p: root("score-partwise"_).children) {
		if(p.name!="part"_) continue;

		KeySignature keySignature = 0; TimeSignature timeSignature={4,4};
		uint measureTime = 0, time = 0, nextTime = 0, maxTime = 0;
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
			int lastChordTime=0; Step minStep, maxStep;
			Tuplet tuplet {0,{},{},{},{}};
			for(const Element& e: m.children) {
				if(!(e.name=="note"_ && e.contains("chord"_))) time = nextTime; // Advances time (except chords)
				maxTime = max(maxTime, time);

				if(e.name=="note"_) {
					Value value = e.contains("type"_) ? Value(ref<string>(valueNames).indexOf(e("type"_).text())) : Whole;
					assert_(int(value)!=-1);
					int duration;
					uint durationCoefficientNum=1, durationCoefficientDen=1;
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
							durationCoefficientNum = parseInteger(e("time-modification"_)("normal-notes").text());
							durationCoefficientDen = parseInteger(e("time-modification"_)("actual-notes").text());
							notationDuration = notationDuration * durationCoefficientNum / durationCoefficientDen;
							assert_(e("time-modification"_)("normal-type"_).text() == e("type"_).text());
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
#if 0
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
#endif
					uint staff = 1-xmlStaffIndex; // Inverts staff order convention: (top/treble, bottom/bass) -> (bottom/bass, top/treble)
					assert_(staff < staffCount, staff);
					assert_(int(value)>=0, e);
					if(e.contains("rest"_)) insertSign({Sign::Rest, time, {{staff, {{duration, .rest={value}}}}}});
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

						// Chord
						if(time != lastChordTime) {
							lastChordTime = time;
							minStep=maxStep={staff, step};
							if(tuplet.size) tuplet.size++;
						}
						if(staff <= minStep.staff) minStep = {staff, min(minStep.step, step)};
						if(staff >= maxStep.staff) maxStep = {staff, max(maxStep.step, step)};

						// Tuplet
						if(tuplet.size) {
							if(time == tuplet.first.time) {
								if(staff <= tuplet.first.min.staff) tuplet.first.min = {staff, min(tuplet.first.min.step, step)};
								if(staff >= tuplet.first.max.staff) tuplet.first.max = {staff, max(tuplet.first.max.step, step)};
							}
							if(staff <= tuplet.min.staff) tuplet.min = {staff, min(tuplet.min.step, step)};
							if(staff >= tuplet.max.staff) tuplet.max = {staff, max(tuplet.max.step, step)};
						}
						if(e.contains("notations"_) && e("notations"_).contains("tuplet"_)) {
							if(e("notations"_)("tuplet"_)["type"_]=="start") {
								assert_(!tuplet.size);
								tuplet = {1,{time, {staff, step}, {staff, step}}, {time, {staff, step}, {staff, step}}, {staff, step}, {staff, step}};
							}
							if(e("notations"_)("tuplet"_)["type"_]=="stop") {
								assert_(tuplet.size);
								tuplet.last = {time, minStep, maxStep};
								insertSign({Sign::Tuplet, time, {.tuplet=tuplet}});
								tuplet.size = 0;
							}
						}

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
						int implicitAlteration = ::implicitAlteration(keySignature, measureAlterations, step);

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
							//log("Alteration mismatch", alteration, xmlAlteration);
							assert_(xmlAlteration ==0 && (alteration==-1 || alteration==1));
							accidental = alterationAccidental(xmlAlteration);
							alteration = xmlAlteration;
						}

						// Redundant accidental
						//if(accidental == implicitAccidental) accidental = Accidental::None;

						// Records alteration used for the measure
						if(accidental) measureAlterations[step] = alteration;

						bool articulations = e.contains("notations"_) && e("notations"_).contains("articulations"_);
						bool ornaments = e.contains("notations"_) && e("notations"_).contains("ornaments"_);
						{
							Sign sign{Sign::Note, time, {{staff, {{duration, .note={
													.value=value,
													.clef=clefs[staff],
													.step=step,
													.alteration=alteration,
													.accidental=accidental,
													.tie=tie,
													.durationCoefficientNum = (uint)durationCoefficientNum,
													.durationCoefficientDen = (uint)durationCoefficientDen,
													.dot = e.contains("dot"_) ? true : false,
													.grace = e.contains("grace"_)?true:false,
													.acciaccatura = e.contains("grace"_) && e("grace"_)["slash"_]=="yes"_?true:false,
													.accent= articulations && e("notations"_)("articulations"_).contains("accent"_)?true:false,
													.staccato= articulations && e("notations"_)("articulations"_).contains("staccato"_)?true:false,
													.tenuto= articulations && e("notations"_)("articulations"_).contains("tenuto"_)?true:false,
													.trill=ornaments && e("notations"_)("ornaments"_).contains("trill-mark"_)?true:false,
													.finger = fingering ? fingering.take(0) : 0
													//.stem = e.contains("stem"_) && e("stem").text() == "up"_,
												}}}}}};
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
							insertSign({Sign::Dynamic, time, {.dynamic=dynamic}});
						}
						else if(d.contains("metronome"_)) {
							Value beatUnit = Value(ref<string>({"whole"_,"half"_,"quarter"_,"eighth"_,"16th"_})
												   .indexOf(d("metronome"_)("beat-unit"_).text()));
							uint perMinute = parseInteger(d("metronome"_)("per-minute"_).text());
							insertSign({Sign::Metronome, time, {.metronome={beatUnit, perMinute}}});
						}
						else if(d.contains("pedal"_)) {
							Pedal pedal = Pedal(ref<string>({"start"_,"change"_,"stop"_}).indexOf(d("pedal"_)["type"_]));
							if(pedal==Start && d("pedal"_)["line"_]!="yes"_) pedal=Ped;
							int offset = e.contains("offset"_) ? parseInteger(e("offset"_).text()) : 0;
							if((offset+1)%(divisions/2) == 0) offset++; // FIXME
							insertSign({Sign::Pedal, time + offset, {.pedal=pedal}});
						}
						else if(d.contains("wedge"_)) {
							Wedge wedge = Wedge(ref<string>({"crescendo"_,"diminuendo"_,"stop"_}).indexOf(d("wedge"_)["type"_]));
							insertSign({Sign::Wedge, time, {.wedge=wedge}});
						}
						else if(d.contains("octave-shift"_)) {
							OctaveShift octave = OctaveShift(ref<string>({"down"_,"up"_,"stop"_}).indexOf(d("octave-shift"_)["type"_]));
							uint xmlStaffIndex = e.contains("staff") ? parseInteger(e("staff"_).text())-1 : partIndex;
							uint staff = 1-xmlStaffIndex; // Inverts staff order convention: (top/treble, bottom/bass) -> (bottom/bass, top/treble)
							assert_(staff < staffCount, staff);
							if(octave == Down) clefs[staff].octave++;
							if(octave == Up) clefs[staff].octave--;
							if(octave == OctaveStop) {
								assert_(octaveStart[staff].octave==Down || octaveStart[staff].octave==Up);
								if(octaveStart[staff].octave == Down) clefs[staff].octave--;
								if(octaveStart[staff].octave == Up) clefs[staff].octave++;
							}
							octaveStart[staff] = signs[insertSign({Sign::OctaveShift, time, {{xmlStaffIndex, {.octave=octave}}}})];
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
						insertSign({Sign::Metronome, time, .metronome={Quarter, uint(parseDecimal(e("sound"_).attribute("tempo"_)))}});
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
						insertSign({Sign::Clef, time, {{staff,  .clef={clefSign, octave}}}});
						clefs[staff] = {clefSign, octave};
					});
					if(e.contains("key"_)) {
						keySignature = parseInteger(e("key"_)("fifths"_).text());
						if(partIndex==0) insertSign({Sign::KeySignature, time, .keySignature=keySignature});
					}
					if(e.contains("time"_)) {
						timeSignature = {uint(parseInteger(e("time"_)("beats"_).text())), uint(parseInteger(e("time"_)("beat-type"_).text()))};
						if(partIndex==0) insertSign({Sign::TimeSignature, time, .timeSignature=timeSignature});
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
								int repeatLength = time - signs[repeatIndex].time; // FIXME: Assumes document order matches time order
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
							if(partIndex==0) insertSign({Sign::Repeat, time, .repeat=Repeat::Begin});
						}
						else if(e("repeat")["direction"]=="backward") {
							if(partIndex==0) insertSign({Sign::Repeat, time, .repeat=Repeat::End});
						}
						else error(e);
					}
					if(e.contains("ending") && e("ending")["type"]=="start") {
						if(partIndex==0) insertSign({Sign::Repeat, time, .repeat=Repeat(parseInteger(e("ending")["number"]))});
					}
#endif
				}
				else if(e.name=="harmony"_) {}
				else error(e);

				assert_(time >= measureTime, int(time-measureTime), int(nextTime-measureTime), int(maxTime-measureTime), globalMeasureIndex, e);
			}
			maxTime=time=nextTime= max(maxTime, max(time, nextTime));
			Measure::Break measureBreak = Measure::NoBreak;
			if(m.contains("print")) {
				if(m("print")["new-page"]=="yes") measureBreak=Measure::PageBreak;
				else if(m("print")["new-system"]=="yes") measureBreak = Measure::LineBreak;
			}
			assert_(time > measureTime);
			if(partIndex == 0) insertSign({Sign::Measure, time, .measure={measureBreak, globalMeasureIndex, pageIndex, lineIndex, measureIndex}});
		}
		partIndex++;
	}

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
	{ // Reorders clefs change to appear before elements on other staff to prevent unecessary clears
		size_t staffIndex[staffCount] = {0, 0};
		for(size_t signIndex : range(signs.size)) {
			Sign sign = signs[signIndex];
			if(sign.type==Sign::Clef && signIndex > staffIndex[sign.staff]+1) {
				//log(staffIndex[sign.staff], signIndex);
				//log(signs.slice(staffIndex[sign.staff]-1, (signIndex+1)-(staffIndex[sign.staff]-1)));
				for(size_t index: reverse_range(signIndex, staffIndex[sign.staff]+1)) signs[index] = signs[index-1];
				signs[staffIndex[sign.staff]+1] = sign;
				//log(signs.slice(staffIndex[sign.staff]-1, signIndex+1-(staffIndex[sign.staff]-1)));
			}
			if(sign.type == Sign::Clef || sign.type == Sign::OctaveShift || sign.type==Sign::Note || sign.type==Sign::OctaveShift) {
				staffIndex[sign.staff] = signIndex;
			} else {
				for(size_t staff : range(staffCount)) staffIndex[staff] = signIndex;
			}
		}
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
	for(size_t signIndex : range(signs.size)) {
		Sign& sign = signs[signIndex];
		if(sign.type == Sign::Measure) { page=sign.measure.page, line=sign.measure.pageLine, measure=sign.measure.lineMeasure; }
		if(sign.type == Sign::Note && (sign.note.tie == Note::TieStart)) activeTies.append(signIndex);
		if(sign.type == Sign::Note && (sign.note.tie == Note::TieContinue || sign.note.tie == Note::TieStop)) {
			size_t tieStart = invalid;
			for(size_t index: range(activeTies.size)) {
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
	size_t measureStartIndex=0;
	for(size_t signIndex : range(signs.size)) {
		Sign& sign = signs[signIndex];
		if(sign.type == Sign::Measure) measureStartIndex = signIndex;
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

			map<int, int> measureAlterations; // Currently accidented steps (for implicit accidentals)
			for(size_t index: range(measureStartIndex, signIndex)) {
				const Sign sign = signs[index];
				if(sign.type == Sign::Note) measureAlterations[sign.note.step] = sign.note.alteration;
			}

			// Recomputes accidental to reflect any previous changes in the same measure
			sign.note.accidental =
					(sign.note.alteration == implicitAlteration(keySignature, measureAlterations, sign.note.step) ? Accidental::None :
																													alterationAccidental(sign.note.alteration));

			int key = sign.note.key();
			int step = keyStep(keySignature, key);
			int alteration = keyAlteration(keySignature, key);
			Accidental accidental =
					(alteration == implicitAlteration(keySignature, measureAlterations, step) ? Accidental::None : alterationAccidental(alteration));

			assert_(key == noteKey(sign.note.clef.octave, step, alteration),
					sign.note, sign.note.step, sign.note.alteration, key, step, alteration, noteKey(sign.note.clef.octave, step, alteration),
					strNote(0, step, accidental), strKey(61), strKey(62));

			if(//(accidental == sign.note.accidental) || // No operation
					(accidental && !sign.note.accidental) || // Does not introduce additional accidentals (for ambiguous tones)
					// Only changes an existing accidental to switch to key alteration direction
					(accidental && sign.note.accidental && (accidental < sign.note.accidental) != (keySignature < 0))) continue;
			sign.note.step = step;
			sign.note.alteration = alteration;
			sign.note.accidental = accidental;
		}
	}
#endif

	assert_(signs);
	//log(signs);
}
