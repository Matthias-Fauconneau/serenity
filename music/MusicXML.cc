#include "MusicXML.h"
#include "xml.h"

MusicXML::MusicXML(string document, string) {
    Element root = parseXML(document);
	Clef clefs[2] = {{Treble,0}, {Treble,0}};
	for(uint staff: range(2)) signs.insertSorted(Sign{0, 0, staff, Sign::Clef, .clef=clefs[staff]}); // Defaults
	uint partIndex = 0;
	for(const Element& p: root("score-partwise"_).children) {
		if(p.name!="part"_) continue;

		KeySignature keySignature={0}; TimeSignature timeSignature={4,4};
		int64 measureTime = 0, time = 0, nextTime = 0, maxTime = 0;
		uint measureIndex=0, pageIndex=0, pageLineIndex=0, lineMeasureIndex=0; // starts with 1
		//size_t repeatIndex = invalid;
		array<size_t> activeTies;
		for(const Element& m: p.children) {
			measureTime = time;
			measureIndex++; lineMeasureIndex++;
			assert_(m.name=="measure"_, m);
			map<int, Accidental> measureAccidentals; // Currently accidented steps (for implicit accidentals)
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
							signs.insertSorted(grace);
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
					uint staff = 1-xmlStaffIndex; // Inverts staff order convention (treble, bass -> bass, treble)
					assert_(int(value)>=0, e);
					if(e.contains("rest"_)) signs.insertSorted({time, duration, staff, Sign::Rest, .rest={value}});
					else {
						assert_(e("pitch"_)("step"_).text(), e);
						uint octaveStep = "CDEFGAB"_.indexOf(e("pitch"_)("step"_).text()[0]);
						int noteOctave = parseInteger(e("pitch"_)("octave"_).text());
						int noteStep = (noteOctave-4) * 7 + octaveStep;
						Accidental noteAccidental = e.contains("accidental"_) ?
									Accidental(ref<string>(accidentalNames).indexOf(e("accidental"_).text())) : None;
						assert_(noteAccidental != -1, e("accidental"_));

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
						uint key = ({// Converts note to MIDI key
									 int step = noteStep;
									 int octave = clefs[staff].octave + step>0 ? step/7 : (step-6)/7; // Rounds towards
									 step = (step - step/7*7 + 7)%7;
									 uint stepToKey[] = {0,2,4,5,7,9,11}; // C [C#] D [D#] E F [F#] G [G#] A [A#] B
									 //assert_(step>=0 && step<8, step, midiIndex, midiKey);
									 uint key = 60 + octave*12 + stepToKey[step];

									 Accidental accidental = None;
									 int fifths = keySignature.fifths;
									 for(int i: range(abs(fifths))) {
										 int fifthStep = fifths<0 ? 4+(3*i+2)%7 : 6+(4*i+4)%7;
										 if(step == fifthStep%7) accidental = fifths>0?Sharp:Flat;
									 }
									 if(noteAccidental!=None) measureAccidentals[noteStep] = noteAccidental;
									 accidental = measureAccidentals.value(noteStep, accidental); // Any accidental overrides key signature
									 if(accidental==Flat) key--;
									 if(accidental==Sharp) key++;
									 key;
									});
						bool articulations = e.contains("notations"_) && e("notations"_).contains("articulations"_);
						bool ornaments = e.contains("notations"_) && e("notations"_).contains("ornaments"_);
						{Sign sign{time, duration, staff, Sign::Note, .note={clefs[staff], noteStep, noteAccidental, value, tie,
																			 e.contains("dot"_) ? true : false,
																			 e.contains("grace"_)?true:false,
																			 e.contains("grace"_) && e("grace"_)["slash"_]=="yes"_?true:false,
																			 articulations && e("notations"_)("articulations"_).contains("staccato"_)?true:false,
																			 articulations && e("notations"_)("articulations"_).contains("tenuto"_)?true:false,
																			 articulations && e("notations"_)("articulations"_).contains("accent"_)?true:false,
																			 ornaments && e("notations"_)("ornaments"_).contains("trill-mark"_)?true:false,
																			 e.contains("stem"_) && e("stem").text() == "up"_,
																			 e.contains("time-modification"_) ? (uint)parseInteger(e("time-modification"_)("actual-notes").text()) : 0,
																			 key, invalid, invalid }};
							// Acciaccatura are played before principal beat (Records graces to shift in on parsing principal)
							if(e.contains("grace"_) && e("grace"_)["slash"_]=="yes"_) {
								/*if(e.contains("notations"_) && e("notations"_).contains("slur"_)) {
									const int index = e("notations"_)("slur"_)["number"] ? parseInteger(e("notations"_)("slur"_)["number"]) : -1;
									if(e("notations"_)("slur"_).attribute("type"_)=="start"_) {
										signs.insertSorted({time, 0, staff, Sign::Slur, .slur={signs.size, index, SlurStart, false}});
									}
									else error(e);
								}*/
								acciaccaturas.append( sign ); // FIXME: display after measure bar
							} else {
								/*if(e.contains("notations"_) && e("notations"_).contains("slur"_)) {
									const int index = e("notations"_)("slur"_)["number"] ? parseInteger(e("notations"_)("slur"_)["number"]) : -1;
									if(e("notations"_)("slur"_).attribute("type"_)=="start"_) {
										signs.insertSorted({time, 0, staff, Sign::Slur, .slur={signs.size, index, SlurStart, false}});
										signs.insertSorted(sign);
									} else if(e("notations"_)("slur"_).attribute("type"_)=="stop"_) {
										signs.insertSorted(sign);
										signs.insertSorted({time, 0, staff, Sign::Slur, .slur={signs.size, index, SlurStop, false}});
									}
									else error(e);
								} else*/ signs.insertSorted(sign);
							}
						}
					}
					if(e.contains("grace"_) && e("grace"_)["slash"_]!="yes"_) appoggiaturaTime += duration; // Takes time away from principal (appoggiatura)
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
							static ref<string> dynamics = {"ppp"_,"pp"_,"p"_,"mp"_,"mf"_,"f"_,"ff"_,"fff"_,"fp"_,"fz"_,"sf"_};
							size_t index = dynamics.indexOf(d("dynamics"_).children.first()->name);
							assert_(index!=invalid, d);
							signs.insertSorted({time, 0, uint(-1), Sign::Dynamic, .dynamic=dynamics[index]});
						}
						else if(d.contains("metronome"_)) {
							Value beatUnit = Value(ref<string>({"whole"_,"half"_,"quarter"_,"eighth"_,"16th"_})
												   .indexOf(d("metronome"_)("beat-unit"_).text()));
							uint perMinute = parseInteger(d("metronome"_)("per-minute"_).text());
							signs.insertSorted({time, 0, uint(-1), Sign::Metronome, .metronome={beatUnit, perMinute}});
						}
						else if(d.contains("pedal"_)) {
							Pedal pedal = Pedal(ref<string>({"start"_,"change"_,"stop"_}).indexOf(d("pedal"_)["type"_]));
							if(pedal==Start && d("pedal"_)["line"_]!="yes"_) pedal=Ped;
							int offset = e("offset"_) ? parseInteger(e("offset"_).text()) : 0;
							if((offset+1)%(divisions/2) == 0) offset++; // FIXME
							signs.insertSorted({time + offset, 0, uint(-1), Sign::Pedal, .pedal=pedal});
						}
						else if(d.contains("wedge"_)) {
							Wedge wedge = Wedge(ref<string>({"crescendo"_,"diminuendo"_,"stop"_}).indexOf(d("wedge"_)["type"_]));
							signs.insertSorted({time, 0, uint(-1), Sign::Wedge, .wedge=wedge});
						}
						else if(d.contains("octave-shift"_)) {
							OctaveShift octave = OctaveShift(ref<string>({"down"_,"up"_,"stop"_}).indexOf(d("octave-shift"_)["type"_]));
							signs.insertSorted({time, 0, 0/*FIXME*//*uint(-1)*/, Sign::OctaveShift, .octave=octave});
						}
						else if(d.contains("other-direction"_)) {}
						else if(d.contains("words"_)) {}
						else if(d.contains("rehearsal"_)) {}
						else if(d.contains("bracket"_)) {}
						else error(e);
					}
					if(e.contains("sound"_) && e("sound"_)["tempo"_]) {
						signs.insertSorted({time, 0, uint(-1), Sign::Metronome,
											.metronome={Quarter, uint(parseDecimal(e("sound"_).attribute("tempo"_)))}});
					}
				}
				else if(e.name=="attributes"_) {
					if(e.contains("divisions"_)) divisions = parseInteger(e("divisions"_).text());
					e.xpath("clef"_, [&](const Element& clef) {
						uint xmlStaffIndex = clef["number"_] ? parseInteger(clef["number"_])-1 : partIndex;
						uint staff = 1-xmlStaffIndex; // Inverts staff order convention (treble, bass -> bass, treble)
						assert_(staff >= 0 && staff <= 1);
						ClefSign clefSign = ClefSign("FG"_.indexOf(clef("sign"_).text()[0]));
						signs.insertSorted({time, 0, staff, Sign::Clef, .clef={clefSign, 0}});
						clefs[staff] = {clefSign, 0};
					});
					if(e.contains("key"_)) {
						keySignature.fifths = parseInteger(e("key"_)("fifths"_).text());
						if(partIndex==0) signs.insertSorted({time, 0, uint(-1), Sign::KeySignature, .keySignature=keySignature});
					}
					if(e.contains("time"_)) {
						timeSignature = {uint(parseInteger(e("time"_)("beats"_).text())), uint(parseInteger(e("time"_)("beat-type"_).text()))};
						if(partIndex==0) signs.insertSorted({time, 0, uint(-1), Sign::TimeSignature, .timeSignature=timeSignature});
					}
				}
				else if(e.name=="print"_) {
					if(e["new-system"]=="yes") { pageLineIndex++, lineMeasureIndex=1; }
					if(e["new-page"]=="yes") { pageIndex++, pageLineIndex=1; }
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
													.measure={false, measureIndex, pageIndex, pageLineIndex, lineMeasureIndex}});
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
							if(partIndex==0) signs.insertSorted({time, 0, uint(-1), Sign::Repeat, .repeat=Repeat::Begin});
						}
						else if(e("repeat")["direction"]=="backward") {
							if(partIndex==0) signs.insertSorted({time, 0, uint(-1), Sign::Repeat, .repeat=Repeat::End});
						}
						else error(e);
					}
					if(e.contains("ending") && e("ending")["type"]=="start") {
						if(partIndex==0) signs.insertSorted({time, 0, uint(-1), Sign::Repeat, .repeat=Repeat(parseInteger(e("ending")["number"]))});
					}
#endif
				}
				else if(e.name=="harmony"_) {}
				else error(e);

				assert_(time >= measureTime, int(time-measureTime), int(nextTime-measureTime), int(maxTime-measureTime), measureIndex, e);
			}
			maxTime=time=nextTime= max(maxTime, max(time, nextTime));
			bool lineBreak = m.contains("print") ? m("print")["new-system"]=="yes" : false;
			assert_(time > measureTime);
			if(partIndex == 0)
				signs.insertSorted({time, 0, uint(-1), Sign::Measure, .measure={lineBreak, measureIndex, pageIndex, pageLineIndex, lineMeasureIndex}});
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

#if 0
	{/// Converts ties to longer notes (spanning beats and measures) (FIXME: new note within merged time)
	array<size_t> active;
	uint page=0, line=0, measure=0;
	for(size_t signIndex=0; signIndex < signs.size;) {
		Sign& sign = signs[signIndex];
		if(sign.type == Sign::Measure) { page=sign.measure.page, line=sign.measure.pageLine, measure=sign.measure.lineMeasure; }
		if(sign.type == Sign::Note && (sign.note.tie == Note::TieStart)) active.append(signIndex);
		if(sign.type == Sign::Note && (sign.note.tie == Note::TieContinue || sign.note.tie == Note::TieStop)) {
			size_t tieStart = invalid;
			for(size_t index: range(active.size)) if(signs[active[index]].note.key == sign.note.key) { assert_(tieStart==invalid); tieStart = index; }
			assert_(tieStart != invalid);
			Sign& first = signs[active[tieStart]];
			if(sign.note.tie == Note::TieStop) active.removeAt(tieStart);
			first.duration = sign.time+sign.duration-first.time;
			int duration = first.note.duration() + sign.note.duration();
			bool dot = false;
			if(duration%3 == 0) {
				dot = true;
				duration = duration * 2 / 3;
			}
			if(isPowerOfTwo(duration)) {
				assert_(isPowerOfTwo(duration), first.note.duration(), sign.note.duration(), duration);
				first.note.value = Value(ref<uint>(valueDurations).size-1-log2(duration));
				first.note.dot = dot;
				assert_(int(first.note.value)>=0);
				first.note.tie = Note::NoTie;
				//sign.note.tie = Note::Merged;
				signs.removeAt(signIndex);
				continue;
			}
		}
		signIndex++;
	}
	}
#endif

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

	assert_(signs);
}
