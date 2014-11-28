#include "MusicXML.h"
#include "xml.h"

inline bool isPowerOfTwo(uint v) { return !(v & (v - 1)); }

MusicXML::MusicXML(string document, string) {
    Element root = parseXML(document);
	map<uint, Clef> clefs;
	KeySignature keySignature={0}; TimeSignature timeSignature={4,4};
	int64 measureTime = 0, time = 0, nextTime = 0, maxTime = 0;
	uint measureIndex=0, pageIndex=0, pageLineIndex=0, lineMeasureIndex=0; // starts with 1
	size_t repeatIndex = invalid;
	array<size_t> activeTies;
	for(const Element& m: root("score-partwise"_)("part"_).children) {
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
				Value type = e.contains("type"_) ?
							Value(ref<string>({"double","whole"_,"half"_,"quarter"_,"eighth"_,"16th"_,"32th","64th"}).indexOf(e("type"_).text())) : Whole;
				int duration;
				if(e.contains("grace"_)) {
					//assert_(uint(type)<Sixteenth && divisions%quarterDuration == 0, int(type), divisions);
					duration = valueDurations[uint(type)]*divisions/quarterDuration;
                } else {
					int acciaccaturaTime = 0;
					for(Sign grace: acciaccaturas.reverse()) { // Inserts any pending acciaccatura graces before principal
                        acciaccaturaTime += grace.duration;
						grace.time = time; //FIXME: -acciaccaturaTime;
                        signs.insertSorted(grace);
                    }
					duration = parseInteger(e("duration"_).text());
					bool dot = e.contains("dot"_) ? true : false;
					if(int(type)==-1) {
						uint typeDuration = duration*quarterDuration/divisions;
						if(typeDuration%3 == 0) {
							dot = true;
							typeDuration = typeDuration * 2 / 3;
						}
						type = Value(ref<uint>(valueDurations).size-1-log2(typeDuration));
						assert_(int(type)>=0, duration, divisions, timeSignature.beats, timeSignature.beatUnit,e);
					}
					//log(timeSignature.beats, divisions, notationDuration, e);
					assert_(uint(type) < ref<uint>(valueDurations).size, int(type), e);
					uint notationDuration = valueDurations[uint(type)]*divisions/quarterDuration;
					if(e.contains("rest"_) && type==Whole) notationDuration = timeSignature.beats*divisions;
					if(dot) notationDuration = notationDuration * 3 / 2;
					if(e.contains("time-modification"_)) {
						notationDuration = notationDuration * parseInteger(e("time-modification"_)("normal-notes").text())
								/ parseInteger(e("time-modification"_)("actual-notes").text());
					}
					//if(!e("chord"))
					/*assert_(duration == notationDuration, e,
							duration, notationDuration, divisions, timeSignature.beats, timeSignature.beatUnit, int(type));*/
					duration -= min(duration, appoggiaturaTime); //FIXME
                    assert_(acciaccaturaTime <= duration, acciaccaturaTime, duration, appoggiaturaTime);
                    acciaccaturas.clear();
                    appoggiaturaTime = 0;
                }
				assert_(duration >= 0, duration);
				if(!e.contains("chord"_) && (!e.contains("grace"_) || e("grace"_)["slash"_]!="yes"_)) nextTime = time+duration;
                if(e["print-object"_]=="no"_) continue;
				uint staff = e("staff"_) ? parseInteger(e("staff"_).text())-1 : 0;
                assert_(int(type)>=0, e);
				if(e.contains("rest"_)) signs.insertSorted({time, duration, staff, Sign::Rest, .rest={type}});
				else {
                    assert_(e("pitch"_)("step"_).text(), e);
                    uint octaveStep = "CDEFGAB"_.indexOf(e("pitch"_)("step"_).text()[0]);
					int noteOctave = parseInteger(e("pitch"_)("octave"_).text());
                    int noteStep = (noteOctave-4) * 7 + octaveStep;
					Accidental noteAccidental = e.contains("accidental"_) ?
							Accidental(ref<string>({""_,"flat"_,"natural"_,"sharp"_,"double-flat"_,"double-sharp"_}).indexOf(e("accidental"_).text())) : None;
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
								 int octave = clefs.value(staff, {Treble, 0}).octave + step>0 ? step/7 : (step-6)/7; // Rounds towards
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
					{Sign sign{time, duration, staff, Sign::Note, .note={clefs.value(staff, {Treble, 0}), noteStep, noteAccidental, type, tie,
																		 e.contains("dot"_) ? true : false,
																		 e.contains("grace"_)?true:false,
																		 e.contains("grace"_) && e("grace"_)["slash"_]=="yes"_?true:false,
																		 articulations && e("notations"_)("articulations"_)("staccato"_)?true:false,
																		 articulations && e("notations"_)("articulations"_)("tenuto"_)?true:false,
																		 articulations && e("notations"_)("articulations"_)("accent"_)?true:false,
																		 articulations && e("notations"_)("ornaments"_)("trill-mark"_)?true:false,
																		 e.contains("stem"_) && e("stem").text() == "up"_,
																		 key, invalid, invalid }};
                        // Acciaccatura are played before principal beat (Records graces to shift in on parsing principal)
						if(e.contains("grace"_) && e("grace"_)["slash"_]=="yes"_) {
							if(e.contains("notations"_) && e("notations"_).contains("slur"_)) {
								const int index = e("notations"_)("slur"_)["number"] ? parseInteger(e("notations"_)("slur"_)["number"]) : -1;
								/**/  if(e("notations"_)("slur"_).attribute("type"_)=="start"_) {
									signs.insertSorted({time, 0, staff, Sign::Slur, .slur={signs.size, index, SlurStart, false}});
								}
								else error(e);
							}
							acciaccaturas.append( sign ); // FIXME: display after measure bar
						} else {
							if(e.contains("notations"_) && e("notations"_).contains("slur"_)) {
								const int index = e("notations"_)("slur"_)["number"] ? parseInteger(e("notations"_)("slur"_)["number"]) : -1;
								/**/  if(e("notations"_)("slur"_).attribute("type"_)=="start"_) {
									signs.insertSorted({time, 0, staff, Sign::Slur, .slur={signs.size, index, SlurStart, false}});
									signs.insertSorted(sign);
								} else if(e("notations"_)("slur"_).attribute("type"_)=="stop"_) {
									signs.insertSorted(sign);
									signs.insertSorted({time, 0, staff, Sign::Slur, .slur={signs.size, index, SlurStop, false}});
								}
								else error(e);
							} else signs.insertSorted(sign);
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
                const Element& d = e("direction-type"_);
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
				if(e.contains("sound"_) && e("sound"_)["tempo"_]) {
					signs.insertSorted({time, 0, uint(-1), Sign::Metronome,
										.metronome={Quarter, uint(parseDecimal(e("sound"_).attribute("tempo"_)))}});
				}
            }
            else if(e.name=="attributes"_) {
				if(e.contains("divisions"_)) divisions = parseInteger(e("divisions"_).text());
                e.xpath("clef"_, [&](const Element& clef) {
					uint staff = clef["number"_] ? parseInteger(clef["number"_])-1 : 0;
                    ClefSign clefSign = ClefSign("FG"_.indexOf(clef("sign"_).text()[0]));
					signs.insertSorted({time, 0, staff, Sign::Clef, .clef={clefSign, 0}});
                    clefs[staff] = {clefSign, 0};
                });
				if(e.contains("key"_)) {
					keySignature.fifths = parseInteger(e("key"_)("fifths"_).text());
					signs.insertSorted({time, 0, uint(-1), Sign::KeySignature, .keySignature=keySignature});
                }
				if(e.contains("time"_)) {
					timeSignature = {uint(parseInteger(e("time"_)("beats"_).text())), uint(parseInteger(e("time"_)("beat-type"_).text()))};
					signs.insertSorted({time, 0, uint(-1), Sign::TimeSignature, .timeSignature=timeSignature});
                }
            }
			else if(e.name=="print"_) {
				if(e["new-system"]=="yes") { pageLineIndex++, lineMeasureIndex=1; }
				if(e["new-page"]=="yes") { pageIndex++, pageLineIndex=1; }
			}
			else if(e.name=="barline"_) {
				if(e("repeat")) {
					if(e("repeat")["direction"]=="forward") {
						assert_(repeatIndex==invalid);
						repeatIndex=signs.size;
					}
					else if(e("repeat")["direction"]=="backward") {
						if(repeatIndex==invalid) { signs.clear(); return; } // FIXME
						assert_(repeatIndex!=invalid);
						buffer<Sign> copy = copyRef(signs.slice(repeatIndex));
						assert_(time==nextTime && time==maxTime && time > signs[repeatIndex].time);
						int64 repeatLength = time - signs[repeatIndex].time; // FIXME: Assumes document order matches time order
						for(Sign& sign: copy) sign.time += repeatLength;
						signs.insertSorted({time, 0, uint(-1), Sign::Measure, .measure={measureIndex, pageIndex, pageLineIndex, lineMeasureIndex}});
						signs.append( move(copy) );
						nextTime = time + repeatLength;
						repeatIndex=invalid;
					}
					else error(e);
				}
			}
			else if(e.name=="harmony"_) {}
            else error(e);

			assert_(time >= measureTime, int(time-measureTime), int(nextTime-measureTime), int(maxTime-measureTime), measureIndex, e);
        }
		maxTime=time=nextTime= max(maxTime, max(time, nextTime));
		signs.insertSorted({time, 0, uint(-1), Sign::Measure, .measure={measureIndex, pageIndex, pageLineIndex, lineMeasureIndex}});
	}

	// Matches start-end-stop signs together
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
	}

	{/// Converts ties to longer notes (spanning beats and measures)
		array<size_t> active;
		uint page=0, line=0, measure=0;
		for(size_t signIndex: range(signs.size)) {
			Sign sign = signs[signIndex];
			if(sign.type == Sign::Measure) { page=sign.measure.page, line=sign.measure.pageLine, measure=sign.measure.lineMeasure; }
			if(sign.type == Sign::Note && (sign.note.tie == Note::TieStart)) active.append(signIndex);
			if(sign.type == Sign::Note && (sign.note.tie == Note::TieContinue || sign.note.tie == Note::TieStop)) {
				size_t tieStart = invalid;
				for(size_t index: range(active.size)) if(signs[active[index]].note.key == sign.note.key) { assert_(tieStart==invalid); tieStart = index; }
				//if(tieStart == invalid) continue; //FIXME
				assert_(tieStart != invalid, (int)sign.note.tie, sign.note.key, signs[active[0]], page, line, measure);
				Sign& first = signs[active[tieStart]];
				first.duration = sign.time+sign.duration-first.time;
				int duration = first.note.duration() + sign.note.duration();
				if(duration%3 == 0) {
					//assert_(!first.note.dot, first.note.duration(), sign.note.duration(), duration);
					first.note.dot = true;
					duration = duration * 2 / 3;
				}
				//assert_(isPowerOfTwo(duration), duration); FIXME
				first.note.value = Value(ref<uint>(valueDurations).size-1-log2(duration));
				assert_(int(first.note.value)>=0);
				if(sign.note.tie == Note::TieStop) active.removeAt(tieStart);
			}
		}
	}
}
