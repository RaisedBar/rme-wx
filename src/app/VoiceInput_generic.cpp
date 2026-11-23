// Voice input is only implemented for Windows (SAPI) so far.
#include "VoiceInput.h"

struct VoiceInput::Impl {};

VoiceInput::VoiceInput(wxEvtHandler*) {}
VoiceInput::~VoiceInput() = default;

bool VoiceInput::available() { return false; }
std::vector<VoiceInput::Device> VoiceInput::captureDevices() { return {}; }

void VoiceInput::start(const Options&) {
    if (onStatus) onStatus("Voice control is not available on this system yet", true);
}
void VoiceInput::stop() {}
bool VoiceInput::running() const { return false; }
bool VoiceInput::listening() const { return false; }
wxString VoiceInput::deviceName() const { return {}; }
void VoiceInput::setChannelNames(const std::vector<std::string>&) {}
