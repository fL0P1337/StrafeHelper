#pragma once
/*
 * InputBackend.h
 * Abstract interface for the Interception input backend.
 */

#include <cstdint>

constexpr uint16_t NEO_KEY_MAKE = 0x0000u;
constexpr uint16_t NEO_KEY_BREAK = 0x0001u;
constexpr uint16_t NEO_KEY_E0 = 0x0002u;
constexpr uint16_t NEO_KEY_E1 = 0x0004u;
constexpr uint32_t NEO_SYNTHETIC_INFORMATION = 0x4E454F31u; // 'NEO1'

struct NEO_KEY_EVENT {
  uint16_t scanCode = 0;
  uint16_t flags = 0;
  uint32_t sequence = 0;
  int64_t timestampQpc = 0;

  // Backend-native passthrough metadata.
  int32_t sourceDevice = 0;
  uint16_t nativeState = 0;
  uint16_t _reserved = 0;
  uint32_t nativeInformation = 0;
};

/* Unified status structure */
struct BackendStatus {
  bool driverActive = false;
  long eventsCaptured = 0;
  long eventsDropped = 0;
  long eventsInjected = 0;
};

class InputBackend {
public:
  virtual ~InputBackend() = default;

  /* Initialize connection to the backend driver */
  [[nodiscard]] virtual bool Initialize() noexcept = 0;

  /* Clean shutdown */
  virtual void Shutdown() noexcept = 0;

  /* Poll for events. Fills 'out' with up to 'maxCount' events.
     Returns number of events read. */
  [[nodiscard]] virtual uint32_t PollEvents(NEO_KEY_EVENT *out,
                                            uint32_t maxCount) noexcept = 0;

  /* Pass through an original captured event to the OS stack. */
  [[nodiscard]] virtual bool PassThrough(const NEO_KEY_EVENT &event) noexcept = 0;

  /* Inject a key event */
  [[nodiscard]] virtual bool InjectKey(uint16_t scanCode,
                                       uint16_t flags) noexcept = 0;

  /* Wait for new data to be available (optional block) */
  virtual void WaitForData(uint32_t timeoutMs) noexcept = 0;

  /* Query backend status */
  [[nodiscard]] virtual bool GetStatus(BackendStatus &out) noexcept = 0;

  /* Human-readable backend name (for logs/UI). */
  [[nodiscard]] virtual const char *Name() const noexcept = 0;

  /* Whether backend can suppress physical keys by withholding pass-through. */
  [[nodiscard]] virtual bool CanSuppressPhysical() const noexcept {
    return true;
  }
};
