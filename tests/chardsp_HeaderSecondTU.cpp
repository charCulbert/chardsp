#include "chardsp/chardsp_ADSREnvelope.h"
#include "chardsp/chardsp_BasicSynthVoice.h"
#include "chardsp/chardsp_Biquad.h"
#include "chardsp/chardsp_ColoredNoise.h"
#include "chardsp/chardsp_DCBlocker.h"
#include "chardsp/chardsp_DelayLine.h"
#include "chardsp/chardsp_Detectors.h"
#include "chardsp/chardsp_Downsampler4x.h"
#include "chardsp/chardsp_Gain.h"
#include "chardsp/chardsp_MonoVoiceController.h"
#include "chardsp/chardsp_NomadOscillators.h"
#include "chardsp/chardsp_Oscillator.h"
#include "chardsp/chardsp_Oversampler.h"
#include "chardsp/chardsp_Portamento.h"
#include "chardsp/chardsp_SamplePlayer.h"
#include "chardsp/chardsp_SmoothedValue.h"
#include "chardsp/chardsp_StateVariableFilter.h"
#include "chardsp/chardsp_VoicePool.h"

int chardspHeaderSecondTU()
{
    return static_cast<int> (
        sizeof (chardsp::DelayLine<float, 8>)
        + sizeof (chardsp::Biquad<float>)
        + sizeof (chardsp::SmoothedValue<float>));
}
