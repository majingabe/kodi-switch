/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "speech/ISpeechRecognition.h"
#include "speech/ISpeechRecognitionListener.h"

#include <memory>

namespace
{
class CSpeechRecognitionSwitch : public speech::ISpeechRecognition
{
public:
  void StartSpeechRecognition(
      const std::shared_ptr<speech::ISpeechRecognitionListener>& listener) override
  {
  }
};
} // namespace

std::shared_ptr<speech::ISpeechRecognition> speech::ISpeechRecognition::CreateInstance()
{
  return std::make_shared<CSpeechRecognitionSwitch>();
}
