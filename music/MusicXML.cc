#include "MusicXML.h"
#include "xml.h"

//generic uint argmin(const ref<T>& a) { uint min=0; for(uint i: range(a.size)) if(a[i] < a[min]) min=i; return min; }
generic uint argmax(const ref<T>& a) { uint max=0; for(uint i: range(a.size)) if(a[i] > a[max]) max=i; return max; }

static int implicitAlteration(int keySignature, const map<int, int>& measureAlterations, int step) {
	return measureAlterations.contains(step) ? measureAlterations.at(step) : signatureAlteration(keySignature, step);
}

MusicXML::MusicXML(string document, string) {
    Element root = parseXML(document);

#if 0
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
            if(voiceToStaff.contains(mostCommonVoice)) { // Only use voices for staff assignment when each voice is most common in an unique staff
                voiceToStaff[mostCommonVoice] = staff;
            }
		}
	}
#endif
	root.xpath("score-partwise/part-list/score-part"_, [this](const Element& p) {
		assert_(p.contains("part-name"), p);
		staves.append(p("part-name").text());
	});

    int xmlStaffCount = 0;
    root.xpath("score-partwise/part"_, [&xmlStaffCount](const Element& p) {
            int partStaffCount = 0;
            for(const Element& m: p.children) {
                for(const Element& e: m.children) {
                    if(e.name!="note"_ ) continue;
                    int xmlStaffIndex = e.contains("staff") ? parseInteger(e("staff"_).text())-1 : 0;
                    partStaffCount = max(partStaffCount, xmlStaffIndex+1);
                }
            }
			assert_(partStaffCount <= 2, "#54", partStaffCount);
            xmlStaffCount += partStaffCount;
    });
	//assert_(xmlStaffCount >= 2 && xmlStaffCount <= 3, xmlStaffCount);
	assert_(size_t(xmlStaffCount) == staves.size, xmlStaffCount, staves);

	const size_t staffCount = xmlStaffCount; //2;
	buffer<Clef> clefs(staffCount); clefs.clear(Clef{GClef, 0});
	buffer<Sign> octaveStart(staffCount); octaveStart.clear(Sign{.octave=OctaveStop}); // Current octave shift (for each staff)
    for(uint staff: range(staffCount)) signs.insertSorted({Sign::Clef, 0, {{staff, {.clef=clefs[staff]}}}}); // Defaults
    size_t partIndex = 0, partFirstStaffIndex = 0;
	root.xpath("score-partwise/part"_, [xmlStaffCount, staffCount, this, &partIndex, &partFirstStaffIndex, &clefs, &octaveStart](const Element& p) {
        KeySignature keySignature = 0; TimeSignature timeSignature={4,4};
        uint measureTime = 0, time = 0, nextTime = 0, maxTime = 0;
        uint measureIndex=0, pageIndex=0, lineIndex=0, lineMeasureIndex=0; // starts with 1
        //size_t repeatIndex = invalid;
        array<int> activeTies;
        array<int> fingering;
        int partStaffCount = 0;
        for(const Element& m: p.children) {
            measureTime = time;
            measureIndex++; lineMeasureIndex++;
            assert_(m.name=="measure"_, m);
            map<int, int> measureAlterations; // Currently altered steps (for implicit alterations)
            array<Sign> acciaccaturas; // Acciaccatura graces for pending principal
            int appoggiaturaTime = 0; // Appoggiatura time to remove from pending principal
            uint lastChordTime=0; int minStep = 0, maxStep = 0;
            Tuplet tuplet {0, {0,0}, {0,0}, 0,0}; //{0,{},{},{},{}};

            auto insertSign = [&](Sign sign) {
                int signIndex = signs.insertSorted(sign);
                for(int& index: activeTies) if(signIndex <= index) index++;
                if(signIndex <= minStep) minStep++;
                if(signIndex <= maxStep) maxStep++;
                for(Sign& sign: signs) {
                    if(sign.type == Sign::Tuplet) {
                        Tuplet& tuplet = sign.tuplet;
                        if(signIndex <= tuplet.first.min) tuplet.first.min++;
                        if(signIndex <= tuplet.first.max) tuplet.first.max++;
                        if(signIndex <= tuplet.last.min) tuplet.last.min++;
                        if(signIndex <= tuplet.last.max) tuplet.last.max++;
                        if(signIndex <= tuplet.min) tuplet.min++;
                        if(signIndex <= tuplet.max) tuplet.max++;
                    }
                }
                if(signIndex <= tuplet.first.min) tuplet.first.min++;
                if(signIndex <= tuplet.first.max) tuplet.first.max++;
                if(signIndex <= tuplet.last.min) tuplet.last.min++;
                if(signIndex <= tuplet.last.max) tuplet.last.max++;
                if(signIndex <= tuplet.min) tuplet.min++;
                if(signIndex <= tuplet.max) tuplet.max++;
                return signIndex;
            };

            for(const Element& e: m.children) {
                if(!(e.name=="note"_ && e.contains("chord"_))) time = nextTime; // Advances time (except chords)
                maxTime = max(maxTime, time);

                if(e.name=="note"_) {
                    Value value = e.contains("type"_) ? Value(ref<string>(valueNames).indexOf(e("type"_).text())) : Whole;
					assert_(int(value)!=-1, e);
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
                            //assert_(!e("time-modification"_).contains("normal-type"_) || e("time-modification"_)("normal-type"_).text() == e("type"_).text(), e);
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
					int partStaffIndex = (e.contains("staff") ? parseInteger(e("staff"_).text())-1 : 0);
					partStaffCount = max(partStaffCount, partStaffIndex+1);
					int xmlStaffIndex = partFirstStaffIndex + partStaffIndex;
#if 0
                    if(e.contains("voice")) {
                        uint voiceIndex = parseInteger(e("voice"_).text())-1;
                        if(!voiceToStaff.contains(voiceIndex) && e.contains("rest"_)) continue;
                        if(voiceToStaff.contains(voiceIndex)) {
                            assert_(voiceToStaff.contains(voiceIndex), e, pageIndex, lineIndex, lineMeasureIndex);
                            uint voiceStaffIndex = voiceToStaff.at(voiceIndex);
                            if(voiceStaffIndex != xmlStaffIndex) {
                                log(xmlStaffIndex, voiceStaffIndex, voiceIndex);
                                assert_(voiceStaffIndex < staffCount);
                                xmlStaffIndex = voiceStaffIndex;
                            }
                        }
                    }
#endif
					//if(xmlStaffIndex < xmlStaffCount-2) continue; // Keeps only last two staves
					//uint staff = 1 - max(0, xmlStaffIndex-xmlStaffCount+2); // Merges first staves (i.e only split last staff), inverts staff order to bass,treble
					//assert_(staff < staffCount, staff);
					assert_(xmlStaffIndex <= xmlStaffCount-1);
					uint staff = (xmlStaffCount-1) - xmlStaffIndex;
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
                        int xmlAlteration = e("pitch"_).contains("alter"_) ? parseDecimal(e("pitch"_)("alter"_).text()) : 0;
                        //if(xmlAlteration == 4) xmlAlteration = 2; // ?
                        //if(xmlAlteration == 5) xmlAlteration = 3; // ?
						assert_(xmlAlteration >= -2 && xmlAlteration <= 5, xmlAlteration, e);

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
                                //assert_(tieStart==invalid);
                                tieStart = index;
                            }
                            if(tieStart != invalid) {
                                size_t tieStartNoteIndex = activeTies[tieStart];
                                implicitAlteration = signs[tieStartNoteIndex].note.alteration;
                                if(tie == Note::TieStop) activeTies.removeAt(tieStart);
                            } else if(0) {
                                error("206", e, pageIndex, lineIndex, lineMeasureIndex,
                                      apply(activeTies, [&](size_t index){ return signs[index];}),
                                        apply(activeTies, [&](size_t index){ return signs[index].note.step;}), step);
                            }
                        }

                        // -- Accidental
						Accidental accidental = Accidental::None;
						if(e.contains("accidental"_)) {
							size_t index = ref<string>(SMuFL::accidental).indexOf(e("accidental"_).text());
							assert_(index != invalid, e);
							accidental = Accidental(SMuFL::AccidentalBase + index);
						}

                        int alteration = accidental ? accidentalAlteration(accidental): implicitAlteration;
                        if(xmlAlteration != alteration) {
                            //log("Alteration mismatch", alteration, xmlAlteration);
                            /*assert_((xmlAlteration ==0 && (alteration==-1 || alteration==1)) || ((xmlAlteration==-1||xmlAlteration==1) && alteration==0) ||
                                    (xmlAlteration == 2 && (alteration==0 || alteration==1)) || ((xmlAlteration==3 && (alteration==0||alteration==1))), xmlAlteration, alteration);*/
                            accidental = alterationAccidental(xmlAlteration);
                            alteration = xmlAlteration;
                        }

                        // Redundant accidental
                        if(alteration == implicitAlteration) accidental = Accidental::None;

                        // Records alteration used for the measure
                        if(accidental) measureAlterations[step] = alteration;

                        const Element* notations = e.contains("notations"_) ? &e("notations") : 0;
                        const Element* articulations = notations && notations->contains("articulations"_) ? &notations->child("articulations"_) : 0;
                        const Element* ornaments = notations && notations->contains("ornaments"_) ? &notations->child("ornaments"_) : 0;
                        {
                            Sign sign{Sign::Note, time, {{staff, {{duration, .note={
                                                    .value = value,
                                                    .clef = clefs[staff],
                                                    .step = step,
                                                    .alteration = alteration,
                                                    .accidental = accidental,
                                                    .tie = tie,
                                                    .durationCoefficientNum = (uint)durationCoefficientNum,
                                                    .durationCoefficientDen = (uint)durationCoefficientDen,
                                                    .dot = e.contains("dot"_) ? true : false,
                                                    .grace = e.contains("grace"_)?true:false,
                                                    .acciaccatura = e.contains("grace"_) && e("grace"_)["slash"_]=="yes"_?true:false,
                                                    .accent= articulations && articulations->contains("accent"_)?true:false,
                                                    .staccato = articulations && articulations->contains("staccato"_)?true:false,
                                                    .tenuto = articulations && e("notations"_)("articulations"_).contains("tenuto"_)?true:false,
                                                    .trill = ornaments && e("notations"_)("ornaments"_).contains("trill-mark"_)?true:false,
                                                    .arpeggio = e.contains("notations"_) && e("notations"_).contains("arpeggiate"_)?true:false,
                                                    .finger = fingering ? fingering.take(0) : 0,
                                                    .measureIndex = measureIndex
                                                    //.stem = e.contains("stem"_) && e("stem").text() == "up"_,
                                                }}}}}};
                            const Element* tremolo = ornaments && ornaments->contains("tremolo"_) ? &ornaments->child("tremolo"_) : 0;
                            if(tremolo) {
                                assert_(parseInteger(tremolo->text()) == 3);
                                auto type = tremolo->attribute("type"_);
                                if(type=="start"_) sign.note.tremolo = Note::Tremolo::Start;
                                else if(type=="stop"_) sign.note.tremolo = Note::Tremolo::Stop;
                                else error(e);
                            }

                            // Acciaccatura are played before principal beat. Records graces to shift in on parsing principal
                            if(sign.note.acciaccatura) acciaccaturas.append( sign ); // FIXME: display after measure bar
                            else {
                                if(sign.note.grace) appoggiaturaTime += duration; // Takes time away from principal (appoggiatura)
                                int signIndex = insertSign( sign );
                                if(tie == Note::TieStart) activeTies.append(signIndex);

                                // Chord
                                if(time != lastChordTime) {
                                    lastChordTime = time;
                                    minStep = maxStep = signIndex;
                                    if(tuplet.size) tuplet.size++;
                                }
                                if(staff < signs[minStep].staff || (staff <= signs[minStep].staff && step < signs[minStep].note.step)) minStep = signIndex;
                                if(staff > signs[maxStep].staff || (staff >= signs[maxStep].staff && step > signs[maxStep].note.step)) maxStep = signIndex;

                                // Tuplet
                                if(tuplet.size) {
                                    if(time == signs[tuplet.first.min].time) {
                                        if(staff < signs[tuplet.first.min].staff || (staff <= signs[tuplet.first.min].staff && step < signs[tuplet.first.min].note.step)) tuplet.first.min = signIndex;
                                        if(staff > signs[tuplet.first.max].staff || (staff >= signs[tuplet.first.max].staff && step > signs[tuplet.first.max].note.step)) tuplet.first.max = signIndex;
                                    }
                                    if(staff < signs[tuplet.min].staff || (staff <= signs[tuplet.min].staff && step < signs[tuplet.min].note.step)) tuplet.min = signIndex;
                                    if(staff > signs[tuplet.max].staff || (staff >= signs[tuplet.max].staff && step > signs[tuplet.max].note.step)) tuplet.max = signIndex;
                                }
                                if(e.contains("notations"_) && e("notations"_).contains("tuplet"_)) {
                                    if(e("notations"_)("tuplet"_)["type"_]=="start") {
                                        //assert_(!tuplet.size);
                                        //tuplet = {1,{time, {staff, step}, {staff, step}}, {time, {staff, step}, {staff, step}}, {staff, step}, {staff, step}};
                                        tuplet = {1, {signIndex, signIndex}, {signIndex, signIndex}, signIndex, signIndex};
                                    }
                                    if(e("notations"_)("tuplet"_)["type"_]=="stop") {
                                        if(tuplet.size) {
                                            assert_(tuplet.size);
                                            tuplet.last = {minStep, maxStep};
                                            insertSign({Sign::Tuplet, time, {.tuplet=tuplet}});
                                            tuplet.size = 0;
                                        }
                                    }
                                }
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
                            TextData s = d("metronome"_)("per-minute"_).text();
                            s.whileNo("0123456789"_);
                            float perMinute = s.decimal();
                            assert_(perMinute==uint(perMinute));
                            insertSign({Sign::Metronome, time, {.metronome={beatUnit, uint(perMinute)}}});
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
                            int xmlStaffIndex = e.contains("staff") ? parseInteger(e("staff"_).text())-1 : partIndex;
							//uint staff = 1 - max(0, xmlStaffIndex-xmlStaffCount+2); // Merges first staves (i.e only split last staff), inverts staff order to bass,treble
							//assert_(staff < staffCount, staff);
							assert_(xmlStaffIndex <= xmlStaffCount-1);
							uint staff = (xmlStaffCount-1) - xmlStaffIndex;
                            if(octave == Down) clefs[staff].octave++;
                            if(octave == Up) clefs[staff].octave--;
                            if(octave == OctaveStop) {
                                assert_(octaveStart[staff].octave==Down || octaveStart[staff].octave==Up);
                                if(octaveStart[staff].octave == Down) clefs[staff].octave--;
                                if(octaveStart[staff].octave == Up) clefs[staff].octave++;
                            }
                            octaveStart[staff] = signs[insertSign({Sign::OctaveShift, time, {{staff, {.octave=octave}}}})];
                        }
                        else if(d.contains("other-direction"_)) {}
                        else if(d.contains("rehearsal"_)) {}
                        else if(d.contains("bracket"_)) {}
                        else if(d.contains("words"_)) {
                            d.xpath("words", [&fingering](const Element& words) {
                                if(isInteger(words.text())) fingering.append(parseInteger(words.text())); // Fingering
                                // else { TODO: directions }
                            });
                        }
                        else error(e);
                    }
                    if(e.contains("sound"_) && e("sound"_)["tempo"_]) {
                        insertSign({Sign::Metronome, time, .metronome={Quarter, uint(parseDecimal(e("sound"_).attribute("tempo"_)))}});
                    }
                }
                else if(e.name=="attributes"_) {
                    if(e.contains("divisions"_)) divisions = parseInteger(e("divisions"_).text());
                    e.xpath("clef"_, [&](const Element& clef) {
                        int xmlStaffIndex = partFirstStaffIndex + (clef["number"_] ? parseInteger(clef["number"_])-1 : 0);
						//if(xmlStaffIndex < xmlStaffCount-2) return; // Keeps only last two staves
						//uint staff = min(1, (xmlStaffCount-1) - xmlStaffIndex); // Merges first staves (i.e only split last staff), inverts staff order to bass,treble
						assert_(xmlStaffIndex <= xmlStaffCount-1, partFirstStaffIndex, xmlStaffIndex, xmlStaffCount);
						uint staff = (xmlStaffCount-1) - xmlStaffIndex;
						//assert_(staff >= 0 /*&& staff <= 1*/, staff, xmlStaffIndex, xmlStaffCount);
                        size_t index = "FG"_.indexOf(clef("sign"_).text()[0]);
                        if(index == invalid) { // Filters first parts with C clef
                            assert_(clef("sign"_).text() == "C"_);
                            //assert_(!signs, signs, partIndex, staff);
                            return; // Skip part
                        }
                        ClefSign clefSign = ref<ClefSign>{FClef,GClef}[index];
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
                    if(e["new-system"]=="yes") { lineIndex++, lineMeasureIndex=1; }
                    if(e["new-page"]=="yes") { pageIndex++, lineIndex=1; }
                }
                else if(e.name=="barline"_) {
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
                }
                else if(e.name=="harmony"_) {}
                else error(e);

                assert_(time >= measureTime, int(time-measureTime), int(nextTime-measureTime), int(maxTime-measureTime), measureIndex, e);
            }
            maxTime=time=nextTime= max(maxTime, max(time, nextTime));
            Measure::Break measureBreak = Measure::NoBreak;
            if(m.contains("print")) {
                if(m("print")["new-page"]=="yes") measureBreak=Measure::PageBreak;
                else if(m("print")["new-system"]=="yes") measureBreak = Measure::LineBreak;
            }
            assert_(time > measureTime);
            if(partIndex == 0) insertSign({Sign::Measure, time, .measure={measureBreak, measureIndex, pageIndex, lineIndex, lineMeasureIndex}});
        }
        partIndex++;
		assert_(partStaffCount <= 2, partStaffCount);
        partFirstStaffIndex += partStaffCount;
    });

#if 1
    // Converts absolute references to relative references
    for(int signIndex: range(signs.size)) {
        Sign& sign = signs[signIndex];
        if(sign.type == Sign::Tuplet) {
            Tuplet& tuplet = sign.tuplet;
            tuplet.first.min = tuplet.first.min - signIndex;
            tuplet.first.max = tuplet.first.max - signIndex;
            tuplet.last.min = tuplet.last.min - signIndex;
            tuplet.last.max = tuplet.last.max - signIndex;
            tuplet.min = tuplet.min - signIndex;
            tuplet.max = tuplet.max - signIndex;

        }
    }
#endif

#if 1 // FIXME: update references (tuplet)
    // Removes unused clef change and dynamics
    for(size_t signIndex=0; signIndex < signs.size;) {
        Sign& sign = signs[signIndex];
        if(sign.type==Sign::Clef || sign.type==Sign::Dynamic) {
            size_t nextIndex = signIndex+1;
            while(nextIndex < signs.size && signs[nextIndex].type!=Sign::Note && (signs[nextIndex].type!=sign.type || (sign.type == Sign::Clef && signs[nextIndex].staff != sign.staff)))
                nextIndex++;
            if(nextIndex==signs.size || signs[nextIndex].type==sign.type) {
                signs.removeAt(signIndex);
                continue;
            }
        }
        signIndex++;
	}
#endif

#if 0
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

#if 0 // FIXME: update absolute references (tuplet)
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
                if(signs[activeTies[index]].note.step == sign.note.step) {
                    //assert_(tieStart==invalid);
                    tieStart = index;
                }
			}
            if(tieStart != invalid) {
                assert_(tieStart != invalid, "460", page, line, measure, activeTies, apply(activeTies, [&](size_t index){ return signs[index];}), sign);
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
                if(signs[activeTies[index]].note.step == sign.note.step) {
                    //assert_(tieStart==invalid);
                    tieStart = index;
                }
			}
            if(tieStart != invalid) {
                assert_(tieStart != invalid, "503", page, line, measure, activeTies, apply(activeTies, [&](size_t index){ return signs[index];}), sign);
                sign.note.tieStartNoteIndex = activeTies[tieStart];
                if(sign.note.tie == Note::TieStop) activeTies.removeAt(tieStart);
            }
        }
	}
	}
#endif

#if 1 // Converts accidentals to match key signature (pitch class). Tie support needs explicit tiedNoteIndex to match ties while editing steps
	KeySignature keySignature = 0;
    size_t measureStartIndex=0;
    map<int, int> previousMeasureAlterations; // Currently accidented steps (for implicit accidentals)
    for(size_t signIndex : range(signs.size)) {
        map<int, int> measureAlterations; // Currently accidented steps (for implicit accidentals)
        map<int, int> sameAlterationCount; // Alteration occurence count
        for(size_t index: range(measureStartIndex, signIndex)) {
            const Sign sign = signs[index];
            if(sign.type == Sign::Note) {
                sameAlterationCount[sign.note.step]++;
                if(sign.note.accidental && measureAlterations[sign.note.step] != accidentalAlteration(sign.note.accidental)) {
                    measureAlterations[sign.note.step] = accidentalAlteration(sign.note.accidental);
                    sameAlterationCount[sign.note.step] = 0;
                }
            }
        }

        Sign& sign = signs[signIndex];
        if(sign.type == Sign::Measure) { measureStartIndex = signIndex; previousMeasureAlterations = move(measureAlterations); }
		if(sign.type == Sign::KeySignature) keySignature = sign.keySignature;
		if(sign.type == Sign::Note) {
			if(sign.note.tie == Note::TieContinue || sign.note.tie == Note::TieStop)  {
                if(sign.note.tieStartNoteIndex) {
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
			}

            auto measureAccidental = [&](int step, int alteration) {
                return (alteration == implicitAlteration(keySignature, measureAlterations, step)
                        && (!measureAlterations.contains(step) || sameAlterationCount[step] > 1) // Repeats measure alterations once
                        && alteration == previousMeasureAlterations.value(step, alteration)) // Courtesy accidental
                        //&& TODO: courtesy accidentals for white to white key alteration (Cb, Fb, B#, E#)
                        ? Accidental::None :
                          alterationAccidental(alteration);
            };
            auto courtesyAccidental = [&](int step, int alteration) {
                return alteration == implicitAlteration(keySignature, measureAlterations, step)
                        && ((measureAlterations.contains(step) && sameAlterationCount[step] <= 1) // Repeats measure alterations once
                            || alteration != previousMeasureAlterations.value(step, alteration)); // Courtesy accidental
            };

            // Recomputes accidental to reflect any previous changes to implicit alterations in the same measure
            sign.note.accidental = measureAccidental(sign.note.step, sign.note.alteration);
            sign.note.accidentalOpacity = courtesyAccidental(sign.note.step, sign.note.alteration) ? 1./2 : 1;

			int key = sign.note.key();
            int step = keyStep(keySignature, key) - sign.note.clef.octave*7;
			int alteration = keyAlteration(keySignature, key);
            Accidental accidental = measureAccidental(step, alteration);

            //assert(!sign.note.clef.octave, sign.note.clef.octave, sign.note.step);
            assert_(key == noteKey(sign.note.clef.octave, step, alteration),
                    keySignature, sign.note, sign.note.clef.octave, sign.note.step, sign.note.alteration, key, step, alteration, noteKey(sign.note.clef.octave, step, alteration),
                    strNote(0, step, accidental), strKey(101), strKey(113));

            if((accidental && !sign.note.accidental) // Does not introduce additional accidentals (for ambiguous tones)
                    || (accidental && sign.note.accidental  // Restricts changes to an existing accidental ...
                        && (sign.note.accidental < accidental) == (keySignature < 0) // if already aligned with key alteration direction
                        && sign.note.accidental < Accidental::DoubleSharp)) { // and if simple accidental (explicit complex accidentals)
                if(previousMeasureAlterations.contains(sign.note.step)) previousMeasureAlterations.remove(sign.note.step); // Do not repeat courtesy accidentals
                continue;
            }
			sign.note.step = step;
			sign.note.alteration = alteration;
			sign.note.accidental = accidental;
            sign.note.accidentalOpacity = courtesyAccidental(sign.note.step, sign.note.alteration) ? 1./2 : 1;
            if(previousMeasureAlterations.contains(step)) previousMeasureAlterations.remove(step); // Do not repeat courtesy accidentals
		}
	}
#endif

#if 1
    // Trims trailing rests
    size_t lastNoteIndex = 0, lastMeasureIndex = 0;
    for(size_t signIndex: range(signs.size)) {
        Sign sign = signs[signIndex];
        if(sign.type == Sign::Note) lastNoteIndex = signIndex;
        if(sign.type == Sign::Measure && lastNoteIndex > lastMeasureIndex) lastMeasureIndex = signIndex;
    }
    signs.size = lastMeasureIndex+1; // Last measure with notes
#endif

#if 0 // FIXME: only on same staff
	// Removes double notes
	array<Sign> chord;
	for(size_t signIndex=1; signIndex < signs.size;) {
		Sign sign = signs[signIndex];
		if(sign.type == Sign::Note) {
			if(chord && sign.time != chord[0].time) chord.clear();
			bool contains = false;
			for(Sign o: chord) if(sign.note.step == o.note.step) {
				log("double", o, o.duration, sign, sign.duration, sign.note.measureIndex);
				contains = true;
				break;
			}
			if(contains) { signs.removeAt(signIndex); continue; }
			chord.append( sign );
		}
		signIndex++;
	}
#endif

	assert_(signs);
}
