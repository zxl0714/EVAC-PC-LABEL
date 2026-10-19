#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

// Explicit loopback-development diagnostic. No persistence or server changes.
bool ConfigureNativeCommunityRoundAudit();
void InstallNativeCommunityRoundAudit(unsigned char* image, std::size_t bytes,
                                     std::span<const std::uint8_t, 32> source_sha256);
bool CommunityRoundAuditSelfTest();
