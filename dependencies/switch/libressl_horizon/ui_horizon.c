#include <openssl/ui.h>

const UI_METHOD* UI_OpenSSL(void)
{
	return UI_null();
}
