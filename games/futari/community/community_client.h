#pragma once
#include "common/community/http_client.h"

// Keep the title's protocol/outbox API while sharing the bounded HTTP worker.
using FutariHttpResult = cave::community::HttpResult;
using FutariLiveClient = cave::community::HttpClient;
bool FutariValidNickname(std::string_view name);
std::string FutariNormalizeNickname(std::string_view name);
std::string FutariRandomNickname();
