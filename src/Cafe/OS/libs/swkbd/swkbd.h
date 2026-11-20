#include "Cafe/OS/RPL/COSModule.h"

void swkbd_render(bool mainWindow);
bool swkbd_hasKeyboardInputHook();
void swkbd_keyInput(uint32 keyCode);

struct SwkbdHostKeyboardRequest
{
	std::string initialText;
	std::string guideText;
	uint32 maxLength;
};

using SwkbdHostKeyboard = std::optional<std::string>(*)(const SwkbdHostKeyboardRequest& request);
void swkbd_setHostKeyboard(SwkbdHostKeyboard keyboard);

namespace swkbd
{
	COSModule* GetModule();
}
