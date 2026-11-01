// Included inside TrackerApp::Impl. Minimise uses loop ends, never a PCM selection.
    edit::MinimiseSummary minimiseSummary(int scope,int targetInstrument,int targetSample) const {
        if(scope<0||scope>2)throw std::logic_error("INVALID MINIMISE SCOPE");
        if(scope==2)return edit::analyzeMinimise(doc);
        if(targetInstrument<1||targetInstrument>int(doc.instruments.size()))return {};
        const auto& target=doc.instruments[std::size_t(targetInstrument-1)];
        if(scope==1)return edit::analyzeMinimise(target);
        if(targetSample<0||targetSample>=int(target.samples.size()))return {};
        return edit::analyzeMinimise(target.samples[std::size_t(targetSample)]);
    }
    void minimiseSamples(int scope) {
        finishGesture();
        const int targetInstrument=instrument,targetSample=sample;
        const auto plan=minimiseSummary(scope,targetInstrument,targetSample);
        if(!plan.changed()){notify("NO PCM AFTER ENABLED LOOP ENDS / NOTHING CHANGED");return;}
        const char* titles[]={"MINIMISE SAMPLE?","MINIMISE INSTRUMENT?","MINIMISE ALL INSTRUMENTS?"};
        const auto revision=documentRevision;
        confirm(titles[scope],number(plan.samplesChanged)+" SAMPLES / "+number(plan.bytesRemoved)+" PCM BYTES AFTER LOOP END",
            [this,scope,targetInstrument,targetSample,revision]{
                if(documentRevision!=revision)throw std::runtime_error("SONG CHANGED / RUN MINIMISE AGAIN");
                const auto latest=minimiseSummary(scope,targetInstrument,targetSample);
                if(!latest.changed()){notify("NO PCM AFTER ENABLED LOOP ENDS / NOTHING CHANGED");return;}
                const int beforeStart=selectionStart,beforeEnd=selectionEnd;
                const bool beforeZoom=waveformZoom;
                const auto* beforeSample=currentSample();
                const auto beforeFrames=beforeSample?beforeSample->pcm.size():0;
                try {
                    mutate([&]{
                        if(scope==2)edit::minimise(doc);
                        else if(scope==1)edit::minimise(doc.instruments.at(std::size_t(targetInstrument-1)));
                        else edit::minimise(doc.instruments.at(std::size_t(targetInstrument-1)).samples.at(std::size_t(targetSample)));
                    });
                } catch(...) {selectionStart=beforeStart;selectionEnd=beforeEnd;waveformZoom=beforeZoom;throw;}
                // Follow other PCM edits: existing voices retain their old
                // buffer until retrigger/stop; subsequent notes use the new PCM.
                const auto* afterSample=currentSample();
                if(afterSample&&afterSample->pcm.size()<beforeFrames&&beforeEnd>beforeStart&&selectionEnd<=selectionStart)
                    waveformZoom=false;
                notify("MINIMISED "+number(latest.samplesChanged)+" SAMPLES / "+number(latest.bytesRemoved)+" PCM BYTES REMOVED / UNDO AVAILABLE");
            });
    }
