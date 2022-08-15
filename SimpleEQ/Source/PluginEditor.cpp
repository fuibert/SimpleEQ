/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

void lookAandFeel::drawRotarySlider(juce::Graphics& g,int x, int y, int width, int height,
    float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider) {
    using namespace juce;

    auto bounds = Rectangle<float>(x, y, width, height);

    g.setColour(Colour(97u, 18u, 167u));
    g.fillEllipse(bounds);

    g.setColour(Colour(255u, 54u, 1u));
    g.drawEllipse(bounds, 1.f);

    if (auto* rswl = dynamic_cast<RotarySliderWithLabels*>(&slider)) {
        auto center = bounds.getCentre();

        Path p;

        Rectangle<float> r;
        r.setLeft(center.getX() - 2);
        r.setRight(center.getX() + 2);
        r.setTop(bounds.getY());
        r.setBottom(center.getY() - rswl->getTextHeight() * 1.5);

        p.addRoundedRectangle(r, 2.f);

        jassert(rotaryStartAngle < rotaryEndAngle);

        auto sliderAngleRange = jmap(sliderPosProportional, 0.f, 1.f, rotaryStartAngle, rotaryEndAngle);

        p.applyTransform(AffineTransform().rotated(sliderAngleRange, center.getX(), center.getY()));

        g.fillPath(p);

        g.setFont(rswl->getTextHeight());
        auto text = rswl->getDisplayString();
        auto strWidth = g.getCurrentFont().getStringWidth(text);

        r.setSize(strWidth + 4, rswl->getTextHeight() + 2);
        r.setCentre(bounds.getCentre());

        g.setColour(Colours::black);
        g.fillRect(r);

        g.setColour(Colours::white);
        g.drawFittedText(text, r.toNearestInt(), juce::Justification::centred, 1);
    }
}

//==============================================================================
void RotarySliderWithLabels::paint(juce::Graphics& g) {
    using namespace juce;

    auto startAngle = degreesToRadians(180.f + 45.f);
    auto endAngle = degreesToRadians(180.f - 45.f) + MathConstants<float>::twoPi;

    auto range = getRange();

    auto sliderBounds = getSliderBounds();

    //g.setColour(Colours::red);
    //g.drawRect(getLocalBounds());
    //g.setColour(Colours::yellow);
    //g.drawRect(sliderBounds);

    getLookAndFeel().drawRotarySlider(g, 
        sliderBounds.getX(), sliderBounds.getY(),
        sliderBounds.getWidth(), sliderBounds.getHeight(), 
        jmap(getValue(), 
            range.getStart(), range.getEnd(), 
            0.0, 1.0),
        startAngle, endAngle, 
        *this);

    auto center = sliderBounds.toFloat().getCentre();
    auto radius = sliderBounds.getWidth() * 0.5f;

    g.setColour(Colour(0u, 172u, 1u));
    g.setFont(getTextHeight());

    auto numChoices = labels.size();
    for (int i = 0; i < numChoices; ++i) {
        auto pos = labels[i].pos;
        jassert(0.f <= pos);
        jassert(pos <= 1.f);

        auto angle = jmap(pos, 0.f, 1.f, startAngle, endAngle);

        auto c = center.getPointOnCircumference(radius + getTextHeight() * 0.5f + 1, angle);

        Rectangle<float> r;
        auto str = labels[i].label;
        r.setSize(g.getCurrentFont().getStringWidth(str), getTextHeight());
        r.setCentre(c);
        r.setY(r.getY() + getTextHeight());

        g.drawFittedText(str, r.toNearestInt(), juce::Justification::centred, 1);
    }
}

//==============================================================================
juce::Rectangle<int> RotarySliderWithLabels::getSliderBounds() const {
    auto bounds = getLocalBounds();

    auto size = juce::jmin(bounds.getWidth(), bounds.getHeight());

    size -= getTextHeight() * 2;

    juce::Rectangle<int> r;
    r.setSize(size, size);
    r.setCentre(bounds.getCentreX(), 0);
    r.setY(2);

    return r;
}

//==============================================================================
juce::String RotarySliderWithLabels::getDisplayString() const {
    if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(param))
        return choiceParam->getCurrentChoiceName();

    juce::String str;
    bool addK = false;

    if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param)) {
        float value = getValue();
        if (value >= 1000.f) {
            value /= 1000.f;
            addK = true;
        }

        str = juce::String(value, (addK ? 2 : 0));
    }
    else
        jassertfalse;

    if (suffix.isNotEmpty()) {
        str << " ";
        if (addK)
            str << "k";
        str << suffix;
    }

    return str;
}

//==============================================================================
ResponseCurveComponent::ResponseCurveComponent(SimpleEQAudioProcessor& p) :
    audioProcessor(p),
    leftPathProducer(audioProcessor.leftChannelFifo),
    rightPathProducer(audioProcessor.rightChannelFifo)
{
    const auto& params = audioProcessor.getParameters();
    for (auto param : params)
        param->addListener(this);

    updateChain();

    startTimerHz(60);
}

//==============================================================================
ResponseCurveComponent::~ResponseCurveComponent()
{
    const auto& params = audioProcessor.getParameters();
    for (auto param : params)
        param->removeListener(this);
}
    

//==============================================================================
void ResponseCurveComponent::parameterValueChanged(int parameterIndex, float newValue) {
    parametersChanged.set(true);
}

//==============================================================================
void PathProducer::process(juce::Rectangle<float> fftBounds, double sampleRate) {
    juce::AudioBuffer<float> tempIncomingBuffer;

    while (channelFifo->getNumCompleteBuffersAvailable() > 0) {
        if (channelFifo->getAudioBuffer(tempIncomingBuffer)) {
            auto size = tempIncomingBuffer.getNumSamples();
            juce::FloatVectorOperations::copy(
                monoBuffer.getWritePointer(0, 0),
                monoBuffer.getReadPointer(0, size),
                monoBuffer.getNumSamples() - size);
            juce::FloatVectorOperations::copy(
                monoBuffer.getWritePointer(0, monoBuffer.getNumSamples() - size),
                tempIncomingBuffer.getReadPointer(0, 0),
                size);

            channelFFTDataGenerator.produceFFTDataForRendering(monoBuffer, -48.f);
        }
    }

    const auto fftSize = channelFFTDataGenerator.getFFTSize();
    const auto binWidth = sampleRate / double(fftSize);

    while (channelFFTDataGenerator.getNumAvailableFFTDataBlocks() > 0) {
        std::vector<float> fftData;
        if (channelFFTDataGenerator.getFFTData(fftData))
            pathProducer.generatePath(fftData, fftBounds, fftSize, binWidth, -48.f);
    }

    while (pathProducer.getNumPathsAvailable())
        pathProducer.getPath(channelFFTPath);
}

//==============================================================================
void ResponseCurveComponent::timerCallback() {
    
    auto fftBounds = getAnalysisArea().toFloat();
    auto sampleRate = audioProcessor.getSampleRate();

    leftPathProducer.process(fftBounds, sampleRate);
    rightPathProducer.process(fftBounds, sampleRate);

    if (parametersChanged.compareAndSetBool(false, true)) {
        updateChain();
    }

    repaint();
}

//==============================================================================
void ResponseCurveComponent::updateChain() {
    auto chainSettings = getChainSettings(audioProcessor.apvts);

    monoChain.setBypassed<ChainPositions::LowCut>(chainSettings.lowCutBypassed);
    monoChain.setBypassed<ChainPositions::Peak>(chainSettings.peakBypassed);
    monoChain.setBypassed<ChainPositions::HighCut>(chainSettings.highCutBypassed);

    auto lowCutCoefficients = makeLowCutFilter(chainSettings, audioProcessor.getSampleRate());
    updateCutCoefficients(monoChain.get<ChainPositions::LowCut>(), lowCutCoefficients, chainSettings.lowCutSlope);

    auto peakCoefficients = makePeakFilter(chainSettings, audioProcessor.getSampleRate());
    updateCoefficients(monoChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);

    auto highCutCoefficients = makeHighCutFilter(chainSettings, audioProcessor.getSampleRate());
    updateCutCoefficients(monoChain.get<ChainPositions::HighCut>(), highCutCoefficients, chainSettings.highCutSlope);
}
//==============================================================================
void ResponseCurveComponent::paint(juce::Graphics& g)
{
    using namespace juce;
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll(Colours::black);

    g.drawImage(background, getLocalBounds().toFloat());

    auto responseArea = getAnalysisArea(); 

    auto width = responseArea.getWidth();

    auto& lowcut = monoChain.get<ChainPositions::LowCut>();
    auto& peak = monoChain.get<ChainPositions::Peak>();
    auto& highcut = monoChain.get<ChainPositions::HighCut>();

    auto sampleRate = audioProcessor.getSampleRate();

    std::vector<double> mags;
    mags.resize(width);

    for (int i = 0; i < width; i++)
    {
        double mag = 1.f;
        auto freq = mapToLog10<double>(double(i) / double(width), 20.0, 20000.0);
        if (!monoChain.isBypassed<ChainPositions::LowCut>()) {
            if (!lowcut.isBypassed<0>())
                mag *= lowcut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
            if (!lowcut.isBypassed<1>())
                mag *= lowcut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
            if (!lowcut.isBypassed<2>())
                mag *= lowcut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
            if (!lowcut.isBypassed<3>())
                mag *= lowcut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }

        if (!monoChain.isBypassed<ChainPositions::Peak>())
            mag *= peak.coefficients->getMagnitudeForFrequency(freq, sampleRate);

        if (!monoChain.isBypassed<ChainPositions::HighCut>()) {
            if (!highcut.isBypassed<0>())
                mag *= highcut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
            if (!highcut.isBypassed<1>())
                mag *= highcut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
            if (!highcut.isBypassed<2>())
                mag *= highcut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
            if (!highcut.isBypassed<3>())
                mag *= highcut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }

        mags[i] = Decibels::gainToDecibels(mag);
    }

    Path responseCurve;

    const double outputMin = responseArea.getBottom();
    const double outputMax = responseArea.getY();

    auto map = [outputMin, outputMax](double input)
    {
        return jmap(input, -24.0, 24.0, outputMin, outputMax);
    };

    responseCurve.startNewSubPath(responseArea.getX(), map(mags.front()));

    for (size_t i = 1; i < mags.size(); ++i)
        responseCurve.lineTo(responseArea.getX() + i, map(mags[i]));

    auto leftChannelFFTPath = leftPathProducer.getPath();
    
    leftChannelFFTPath.applyTransform(
        AffineTransform().translation(
            responseArea.getX(), responseArea.getY()
        ));

    g.setColour(Colours::skyblue);
    g.strokePath(leftChannelFFTPath, PathStrokeType(1.f));

    auto rightChannelFFTPath = rightPathProducer.getPath();

    rightChannelFFTPath.applyTransform(
        AffineTransform().translation(
            responseArea.getX(), responseArea.getY()
        ));

    g.setColour(Colours::lightyellow);
    g.strokePath(rightChannelFFTPath, PathStrokeType(1.f));

    g.setColour(Colours::orange);
    g.drawRoundedRectangle(getRenderingArea().toFloat(), 4.f, 1.f);

    g.setColour(Colours::white);
    g.strokePath(responseCurve, PathStrokeType(2.f));
}

//==============================================================================
void ResponseCurveComponent::resized() {
    using namespace juce;
    background = Image(Image::PixelFormat::RGB, getWidth(), getHeight(), true);

    Graphics g(background);

    auto area = getAnalysisArea();
    auto left = area.getX();
    auto right = area.getRight();
    auto top = area.getY();
    auto bottom = area.getBottom();
    auto width = area.getWidth();

    Array<float> freqs{
        20, 50, 100,
        200, 500, 1000,
        2000, 5000, 10000,
        20000
    };

    Array<float> xs;
    for (auto f : freqs)
        xs.add(left + width * mapFromLog10(f, 20.f, 20000.f));

    g.setColour(Colours::dimgrey);
    for (auto f : freqs) {
        g.drawVerticalLine(left + width * mapFromLog10(f, 20.f, 20000.f), top, bottom);
    }

    g.setColour(Colours::lightgrey);
    const int fontHeight = 10;
    g.setFont(fontHeight);
    
    for (int i = 0; i < freqs.size(); ++i) {
        auto f = freqs[i];
        auto x = xs[i];

        bool addK = false;
        String str;
        if (f >= 1000.f) {
            addK = true;
            f /= 1000.f;
        }

        str << f;
        if (addK)
            str << "k";
        str << "Hz";

        auto textWidth = g.getCurrentFont().getStringWidth(str);

        Rectangle<int> r;
        r.setSize(textWidth, fontHeight);
        r.setCentre(x, 0);
        r.setY(1);

        g.drawFittedText(str, r, Justification::centred, 1);
    }

    Array<float> gain{ -24.f, -12.f, 0.f, 12.f, 24.f };

    for (auto gdB : gain) {
        auto y = jmap(gdB, -24.f, 24.f, float(bottom), float(top));
        g.setColour(gdB == 0.f ? Colours::lawngreen : Colours::darkgrey);
        g.drawHorizontalLine(y, left, left + width);

        String str;
        if (gdB > 0)
            str << "+";
        str << gdB << "dB";

        auto textWidth = g.getCurrentFont().getStringWidth(str);

        Rectangle<int> r;
        r.setSize(textWidth, fontHeight);
        r.setX(getWidth() - textWidth);
        r.setCentre(r.getCentreX(), y);

        g.setColour(gdB == 0.f ? Colours::lawngreen : Colours::lightgrey);
        g.drawFittedText(str, r, Justification::centred, 1);

        str.clear();
        str << (gdB + 24.f) << "dB";
        r.setX(1);
        textWidth = g.getCurrentFont().getStringWidth(str);
        r.setSize(textWidth, fontHeight);
        r.setCentre(r.getCentreX(), y);
        g.setColour(Colours::lightgrey);
        g.drawFittedText(str, r, Justification::centred, 1);
    }
}

//==============================================================================
juce::Rectangle<int> ResponseCurveComponent::getRenderingArea() {
    auto bounds = getLocalBounds();

    bounds.removeFromTop(12);
    bounds.removeFromBottom(2);
    bounds.removeFromLeft(30);
    bounds.removeFromRight(30);

    return bounds;
}

juce::Rectangle<int> ResponseCurveComponent::getAnalysisArea() {
    auto bounds = getRenderingArea();
    bounds.removeFromBottom(4);
    bounds.removeFromTop(4);
    return bounds;
}

//==============================================================================
SimpleEQAudioProcessorEditor::SimpleEQAudioProcessorEditor(SimpleEQAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p),
    lowCutFreqSlider(*audioProcessor.apvts.getParameter("LowCut Freq"), "Hz"),
    lowCutSlopeSlider(*audioProcessor.apvts.getParameter("LowCut Slope"), "dB/oct"),
    peakFreqSlider(*audioProcessor.apvts.getParameter("Peak Freq"), "Hz"),
    peakGainSlider(*audioProcessor.apvts.getParameter("Peak Gain"), "dB"),
    peakQualitySlider(*audioProcessor.apvts.getParameter("Peak Quality"), ""),
    highCutFreqSlider(*audioProcessor.apvts.getParameter("HighCut Freq"), "Hz"),
    highCutSlopeSlider(*audioProcessor.apvts.getParameter("HighCut Slope"), "dB/oct"),

    responseCurveComponent(audioProcessor),
    lowCutFreqSliderAttachment(audioProcessor.apvts, "LowCut Freq", lowCutFreqSlider),
    lowCutSlopeSliderAttachment(audioProcessor.apvts, "LowCut Slope", lowCutSlopeSlider),
    lowCutBypassBtnAttachment(audioProcessor.apvts, "LowCut Bypassed", lowCutBypassBtn),
    peakFreqSliderAttachment(audioProcessor.apvts, "Peak Freq", peakFreqSlider),
    peakGainSliderAttachment(audioProcessor.apvts, "Peak Gain", peakGainSlider),
    peakQualitySliderAttachment(audioProcessor.apvts, "Peak Quality", peakQualitySlider),
    peakBypassBtnAttachment(audioProcessor.apvts, "Peak Bypassed", peakBypassBtn),
    highCutFreqSliderAttachment(audioProcessor.apvts, "HighCut Freq", highCutFreqSlider),
    highCutSlopeSliderAttachment(audioProcessor.apvts, "HighCut Slope", highCutSlopeSlider),
    highCutBypassBtnAttachment(audioProcessor.apvts, "HighCut Bypassed", highCutBypassBtn),
    analyzerEnableBtnAttachment(audioProcessor.apvts, "Analyzer Enabled", analyzerEnableBtn)
{
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.

    lowCutFreqSlider.labels.add({ 0.f, "20 Hz" });
    lowCutFreqSlider.labels.add({ 1.f, "20 kHz" });

    lowCutSlopeSlider.labels.add({ 0.f, "12 dB/oct" });
    lowCutSlopeSlider.labels.add({ 1.f, "48 dB/oct" });

    peakFreqSlider.labels.add({ 0.f, "20 Hz" });
    peakFreqSlider.labels.add({ 1.f, "20 kHz" });

    peakGainSlider.labels.add({ 0.f, "-24 dB" });
    peakGainSlider.labels.add({ 1.f, "24 dB" });

    peakQualitySlider.labels.add({ 0.f, "0.1" });
    peakQualitySlider.labels.add({ 1.f, "10.0" });

    highCutFreqSlider.labels.add({ 0.f, "20 Hz" });
    highCutFreqSlider.labels.add({ 1.f, "20 kHz" });

    highCutSlopeSlider.labels.add({ 0.f, "12 dB/oct" });
    highCutSlopeSlider.labels.add({ 1.f, "48 dB/oct" });

    

    for (auto* comp : getComps()) {
        addAndMakeVisible(comp);
    }

    setSize (600, 480);
}

//==============================================================================
SimpleEQAudioProcessorEditor::~SimpleEQAudioProcessorEditor()
{
}

//==============================================================================
void SimpleEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace juce;
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (Colours::black);
}

void SimpleEQAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..

    auto bounds = getLocalBounds();
    float hRatio = 25.f / 100.f; //JUCE_LIVE_CONSTANT(33) / 100.f;
    auto responseArea = bounds.removeFromTop(bounds.getHeight() * hRatio);

    responseCurveComponent.setBounds(responseArea);

    bounds.removeFromTop(5);

    auto lowCutArea = bounds.removeFromLeft(bounds.getWidth() * 0.33);
    auto highCutArea = bounds.removeFromRight(bounds.getWidth() * 0.5);

    lowCutBypassBtn.setBounds(lowCutArea.removeFromTop(25));

    lowCutFreqSlider.setBounds(lowCutArea.removeFromTop(lowCutArea.getHeight() * 0.5));
    lowCutSlopeSlider.setBounds(lowCutArea);

    highCutBypassBtn.setBounds(highCutArea.removeFromTop(25));
    highCutFreqSlider.setBounds(highCutArea.removeFromTop(highCutArea.getHeight() * 0.5));
    highCutSlopeSlider.setBounds(highCutArea);

    peakBypassBtn.setBounds(bounds.removeFromTop(25));
    peakFreqSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.33));
    peakGainSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.5));
    peakQualitySlider.setBounds(bounds);
}

std::vector<juce::Component*> SimpleEQAudioProcessorEditor::getComps()
{
    return
    {
        &lowCutFreqSlider,
        &lowCutSlopeSlider,
        &lowCutBypassBtn,

        &peakFreqSlider,
        &peakGainSlider,
        &peakQualitySlider,
        &peakBypassBtn,

        &highCutFreqSlider,
        &highCutSlopeSlider,
        &highCutBypassBtn,

        &responseCurveComponent,
        &analyzerEnableBtn
    };
}