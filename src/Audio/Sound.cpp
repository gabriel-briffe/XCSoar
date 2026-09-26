// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Audio/Features.hpp"
#include "Audio/Sound.hpp"
#include "Audio/Settings.hpp"
#include "Audio/VarioGlue.hpp"
#include "Audio/VarioSettings.hpp"

#ifdef ANDROID
#include "Android/Main.hpp"
#include "Android/SoundUtil.hpp"
#include "Android/Context.hpp"
#endif

#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include "Apple/SoundUtil.hpp"
#endif
#endif

#if defined(HAVE_PCM_PLAYER)
#include "GlobalPCMResourcePlayer.hpp"
#include "PCMResourcePlayer.hpp"
#elif defined(_WIN32)
// On Windows without SDL we use sndPlaySound
#include "ResourceLoader.hpp"
#include <mmsystem.h>
#endif

#include <atomic>
#include <cstring>

static std::atomic_bool sound_enabled{true};

void
ApplySoundSettings(const SoundSettings &settings) noexcept
{
  sound_enabled.store(settings.enabled, std::memory_order_relaxed);

  VarioSoundSettings vario = settings.vario;
  if (!settings.enabled)
    vario.enabled = false;
  AudioVarioGlue::Configure(vario);
}

bool
IsSoundEnabled() noexcept
{
  return sound_enabled.load(std::memory_order_relaxed);
}

bool
PlayResource(const char *resource_name)
{
  if (!IsSoundEnabled())
    return false;

#ifdef ANDROID

  if (strstr(resource_name, ".wav"))
    return SoundUtil::PlayExternal(Java::GetEnv(), context->Get(), resource_name);
  return SoundUtil::Play(Java::GetEnv(), context->Get(), resource_name);

#elif defined(__APPLE__) && TARGET_OS_IPHONE

  return SoundUtil::Play(resource_name);

#elif defined(HAVE_PCM_PLAYER)

  if (nullptr == pcm_resource_player)
    return false;

  return pcm_resource_player->PlayResource(resource_name);

#elif defined(_WIN32)

  if (strstr(resource_name, TEXT(".wav")))
    return sndPlaySound(resource_name, SND_ASYNC | SND_NODEFAULT);

  ResourceLoader::Data data = ResourceLoader::Load(resource_name, "WAVE");
  return data.data() != nullptr &&
    sndPlaySound((LPCTSTR)data.data(), SND_MEMORY | SND_ASYNC | SND_NODEFAULT);

#else
  return false;
#endif
}
