#pragma once
#include "community/client.h"
#include <span>

struct NativeCommunityRound;
using NativeCommunityRoundHandle = std::shared_ptr<NativeCommunityRound>;
struct NativeCommunityRoundReply {
    bool ready{};
    kt::community::Reply reply;
    std::string submission;
    std::optional<kt::community::Identity> identity;
};
// Startup-only, explicit version-bound loopback producer. Ordinary runs do not
// install or inspect these slots. All worker inputs are owned host values.
bool ConfigureNativeCommunityRounds(std::shared_ptr<kt::community::Client> client);
bool NativeCommunityRoundsEnabled();
void InstallNativeCommunityRounds(unsigned char* image,std::size_t bytes,std::span<const std::uint8_t,32> source);
NativeCommunityRoundHandle CurrentNativeCommunityRound();
bool BeginNativeCommunityRoundScore(const NativeCommunityRoundHandle& round,std::uint32_t board,std::uint64_t score,
                                   std::span<const std::byte,64> comment,std::span<const std::byte> info);
NativeCommunityRoundReply PollNativeCommunityRoundScore(const NativeCommunityRoundHandle& round,std::uint32_t board);
std::future<kt::community::Reply> ProveNativeCommunityRoundReplay(const NativeCommunityRoundHandle& round,std::uint32_t board,
                                                               std::uint64_t score,std::vector<std::byte> payload);
std::size_t NativeCommunityRoundReplayBytes(const NativeCommunityRoundHandle& round);
bool NativeCommunityRoundsSelfTest(const std::filesystem::path& directory);
