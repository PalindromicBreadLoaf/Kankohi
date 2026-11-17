#pragma once

class nexService;

void nexThread_registerService(nexService* service);
bool nexThread_isCurrentThread();
void nexThread_shutdown();
