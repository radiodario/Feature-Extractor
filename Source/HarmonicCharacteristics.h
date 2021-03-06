/*
  ==============================================================================

    HarmonicCharacteristics.h
    Created: 17 Jun 2016 7:59:05pm
    Author:  Sean

  ==============================================================================
*/

#ifndef HARMONICCHARACTERISTICS_H_INCLUDED
#define HARMONICCHARACTERISTICS_H_INCLUDED

struct HarmonicCharacteristics
{
    HarmonicCharacteristics (float her, float oer, float inharm)
    :   harmonicEnergyRatio  (her),
        oddEvenHarmonicRatio (oer),
        inharmonicity        (inharm)
    {}

    float harmonicEnergyRatio;
    float oddEvenHarmonicRatio;
    float inharmonicity;
};

struct HarmonicEnergyCharacteristics
{
    HarmonicEnergyCharacteristics (float her, float oer)
    :   harmonicEnergyRatio (her),
        oddEvenHarmonicRatio (oer)
    {
    }

    float harmonicEnergyRatio;
    float oddEvenHarmonicRatio;
};


class HarmonicCharacteristicsAnalyser
{
public:
    HarmonicCharacteristicsAnalyser () 
    {}

    HarmonicCharacteristics calculateHarmonicCharacteristics (AudioSampleBuffer& fftResults, double f0Estimation, double nyquist, int channel)
    {
        std::vector<int> peakBins;
        peakBins.clear();
        
        jassert (fftResults.getNumSamples() % 2 == 0);
        const int numFFTElements = (int) fftResults.getNumSamples() / 2;
        const int numMagnitudes  = (int) numFFTElements / 2;

        //calculate mean bin magnitude and maximum bin magnitude
        double meanMagnitude = 0.0;
        double magnitudeSum = 0.0;
        double maxMagnitude = 0.0;
        std::vector<double> binMagnitudes ((size_t)numMagnitudes);

        for (int i = 0; i < numMagnitudes; i++)
        {
            double binValue     = (double) fftResults.getSample (channel, i * 2);
            double binMagnitude = binValue * binValue;
            binMagnitudes[i]    = binMagnitude;
            magnitudeSum       += binMagnitude;
            if (binMagnitude > maxMagnitude)
                maxMagnitude = binMagnitude;
        }
        
        AudioSampleBuffer normedMagnitudes (1, numMagnitudes);
        double sumNormedMagnitude = 0.0;
        for (int bin = 0; bin < numMagnitudes; bin++)
        {
            double normedMagnitude = binMagnitudes[bin] / maxMagnitude;
            normedMagnitudes.setSample (0, bin, (float) normedMagnitude);
            sumNormedMagnitude += normedMagnitude;
        }

        if (fftMagnitudesToDrawNeedsUpdating.get() == 1)
        {
            fftMagnitudesToDraw.setSize (1, numMagnitudes);
            fftMagnitudesToDraw = AudioSampleBuffer (normedMagnitudes);
            fftMagnitudesToDrawNeedsUpdating.set (0);
        }
        meanMagnitude = magnitudeSum / (double) numMagnitudes;

        if (magnitudeSum < 0.005)
            return {0.0, 0.0, 0.0};

        fillPeakBins (binMagnitudes, channel, peakBins, meanMagnitude);
        
        double frequencyRangePerBin = nyquist / (double) numMagnitudes;
        HarmonicEnergyCharacteristics h = calculateHarmonicEnergyCharacteristics (/*peakBins*/normedMagnitudes, f0Estimation, frequencyRangePerBin, sumNormedMagnitude, 15.0, 3.0);
        double harmonicEnergyRatio = h.harmonicEnergyRatio;
        double oddEvenHarmonicRatio = h.oddEvenHarmonicRatio;
        double inharmonicity = 0.0;
        if (f0Estimation > 0.0)
            inharmonicity = calculateInharmonicity (binMagnitudes, f0Estimation, frequencyRangePerBin, peakBins, magnitudeSum);

        float logHER    = log10 (harmonicEnergyRatio * 9.0 + 1.0);
        float logInharm = log10 (inharmonicity       * 9.0 + 1.0);
        float logOER    = log10 (oddEvenHarmonicRatio * 9.0 + 1.0);

        return {(float) logHER, (float) logOER, (float) logInharm}; 
    }

    void enableFFTMagnitudesBufferNeedsUpdating()    { fftMagnitudesToDrawNeedsUpdating.set (1); }
    const AudioSampleBuffer getFFTMagnitudesToDraw() { return AudioSampleBuffer (fftMagnitudesToDraw); }

private:
    AudioSampleBuffer fftMagnitudesToDraw;
    Atomic<int>       fftMagnitudesToDrawNeedsUpdating;

    static void fillPeakBins (std::vector<double>& binMagnitudes, int channel, std::vector<int>& peakBins, double meanMagnitude)
    {
        const int numMagnitudes  = (int) binMagnitudes.size();
        for (int bin = 0; bin < numMagnitudes; ++bin)
        {
            bool peak = binIsPeak (bin, binMagnitudes, channel, meanMagnitude);
            
            if (peak)
                peakBins.push_back (bin);
        }
    }

    static bool binIsPeak (int bin, std::vector<double>& binMagnitudes, int channel, double meanMagnitude)
    {
        const int numMagnitudes  = (int) binMagnitudes.size();
        double binMagnitude = binMagnitudes[bin];
        
        if (binMagnitude <= meanMagnitude)
            return false;

        //offsets to ensure no out of bounds checks
        int leftBinOffset  = bin < 2 ? 2 - bin : 0;
        int rightBinOffset = bin >= numMagnitudes - 2 ? 2 - ((numMagnitudes - 1) - bin) : 0; 
        for (int neighbour = bin - (2 - leftBinOffset); neighbour < bin + (2 - rightBinOffset); neighbour++)
        {
            double neighbourMagnitude = binMagnitudes[neighbour];
            if (neighbour != bin && neighbourMagnitude > binMagnitude)
                return false;
        }
        return true;
    }

    static HarmonicEnergyCharacteristics  calculateHarmonicEnergyCharacteristics (AudioSampleBuffer& normalisedBinMagnitudes, 
                                                                                  double f0Estimate,
                                                                                  double frequencyRangePerBin, 
                                                                                  double totalNormedMagnitude,
                                                                                  double numLower,
                                                                                  double numHarmonics)
    {
        double harmonicScore = 0.0;
        double evenHarmonicEnergy = 0.0;
        double oddHarmonicEnergy = 0.0;
        
        //std::vector<double> harmonicEnergyValues;
        //for (int h = 0; h < numHarmonics; h++)
        //    harmonicEnergyValues.push_back (0.0);

        //int peakNum = 0;
        //for (int p : peakBins)
        //{
        //    double peakFreq = p * frequencyRangePerBin + frequencyRangePerBin * 0.5;
        //    if (peakFreq > f0Estimate)
        //    {
        //        double peakRatio = getFrequencyRatio (peakFreq, f0Estimate);

        //    }
        //    peakNum ++;
        //}
        for (double lower = 1.0; lower < numLower + 1.0; ++lower)
        {
            double lowerHarmFreq = f0Estimate / pow(2.0, lower);
            int lowerHarmBin = getBinForFrequency (lowerHarmFreq, frequencyRangePerBin);
            
            if (lowerHarmBin == getBinForFrequency (f0Estimate, frequencyRangePerBin))
                continue;

            double binMagnitude = getMaxBinInNeighbourhood (lowerHarmBin, 2, normalisedBinMagnitudes);//normalisedBinMagnitudes.getSample (0, harmonicBin);
            harmonicScore += binMagnitude;
        }
        for (double harmonic = 1.0; harmonic < numHarmonics + 1.0; harmonic++)
        {
            double harmonicFrequency = f0Estimate * harmonic;
            int harmonicBin = getBinForFrequency (harmonicFrequency, frequencyRangePerBin);

            if (harmonicBin >= normalisedBinMagnitudes.getNumSamples())
                break;

            double binMagnitude = getMaxBinInNeighbourhood (harmonicBin, 2, normalisedBinMagnitudes);//normalisedBinMagnitudes.getSample (0, harmonicBin);
            
            if (int(harmonic) % 2 == 0)
                evenHarmonicEnergy += binMagnitude;
            else
                oddHarmonicEnergy += binMagnitude;

            harmonicScore += binMagnitude;
        }
        double her = harmonicScore / totalNormedMagnitude;
        if (her > 1.0) her = 1.0;
        if (her < 0.0) her = 0.0;

        double oer = 1.0;
        if (oddHarmonicEnergy > 0.0)
            oer = evenHarmonicEnergy / oddHarmonicEnergy;

        if (oer > 1.0) oer = 1.0;
        if (oer < 0.0) oer = 0.0;

        return { (float) her, (float) oer };
    }

    static double getMaxBinInNeighbourhood (int centreBin, int neighbourhoodRange, AudioSampleBuffer& binMagnitudes)
    {
        const int numBins = binMagnitudes.getNumSamples();
        int startIndex = centreBin - neighbourhoodRange >= 0 ? centreBin - neighbourhoodRange : 0;
        int endIndex   = centreBin + neighbourhoodRange < numBins ? centreBin + neighbourhoodRange : numBins;
        double maxMagnitude = binMagnitudes.getSample (0, centreBin);
        for (int b = startIndex; b < endIndex; b++)
            if (binMagnitudes.getSample (0, b) > maxMagnitude)
                maxMagnitude = binMagnitudes.getSample (0, b);
        return maxMagnitude;
    }

    static double calculateInharmonicity (std::vector<double>& binMagnitudes, double f0Estimate, 
                                          double frequencyRangePerBin, std::vector<int>& peakBins, 
                                          double magnitudeSum)
    {
        double inharmonicity = 0.0;
        int f0Bin = getBinForFrequency (f0Estimate, frequencyRangePerBin);
        for (auto bin : peakBins)
        {
            if (f0Bin == bin) //f0 exists in this bin, so don't add any inharmonicity score
                continue;

            double binStartFrequency = bin * frequencyRangePerBin;

            if (binStartFrequency == 0.0)//avoid peak in bin 1 leading to division by 0
                binStartFrequency = frequencyRangePerBin * 0.5;

            double binEndFrequency   = (double) (bin + 1) * frequencyRangePerBin;
            double binStartF0Ratio   = getFrequencyRatio (binStartFrequency, f0Estimate);
            double binEndF0Ratio     = getFrequencyRatio (binEndFrequency, f0Estimate);

            if (floor (binStartF0Ratio) != floor (binEndF0Ratio))//A multiple of f0 exists within this bin
                continue;

            double f0Ratio = binStartF0Ratio < binEndF0Ratio ? binStartF0Ratio : binEndF0Ratio;
            double f0Proportion = f0Ratio - floor (f0Ratio);
            double binEnergyRatio = binMagnitudes[bin] / magnitudeSum;
            jassert (binEnergyRatio < 1.0);
            inharmonicity += f0Proportion * binEnergyRatio;
            jassert (inharmonicity < 1.0);
        }
        jassert (inharmonicity >= 0.0);
        return inharmonicity;
    } 

    static int getBinForFrequency (double freq, double frequencyRangePerBin)
    {
        return (int)(floor(freq / frequencyRangePerBin));
    }

    static double getFrequencyRatio (double frequency1, double frequency2)
    {
        if (frequency1 == frequency2)
            return 1.0;

        double higher = frequency1 > frequency2 ? frequency1 : frequency2;
        double lower  = higher == frequency1 ? frequency2 : frequency1;
        return higher / lower;
    }
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonicCharacteristicsAnalyser)
};


#endif  // HARMONICCHARACTERISTICS_H_INCLUDED
