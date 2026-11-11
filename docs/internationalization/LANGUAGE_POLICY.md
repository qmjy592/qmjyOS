# qmjyOS Language Edition Policy

qmjyOS maintains separate Chinese and international language editions.

## 1. Chinese Edition

- Default language: Simplified Chinese.
- Installer: Simplified Chinese.
- Boot menus: Simplified Chinese.
- Recovery environment: Simplified Chinese.
- System UI: Simplified Chinese.
- Default system messages: Simplified Chinese.

## 2. International Edition

- Default language: English.
- Installer: English.
- Boot menus: English.
- Recovery environment: English.
- System UI: English.
- Default system messages: English.

## 3. Consistency

The international edition must not contain unintended Chinese interface text.

The Chinese edition must not unexpectedly fall back to English
for ordinary system interface messages.

Technical identifiers, source code symbols, protocol names and
developer diagnostics may remain language-neutral.

## 4. Architecture

User-visible strings should be separated from core system logic.

The language edition must be selected through an explicit build
or installation configuration.

System components must not independently choose conflicting
default languages.

## 5. Validation

Both editions require separate validation of:

- Boot menus
- Installer
- Recovery environment
- Login and session UI
- System settings
- Error messages
- Applications and system services

A language configuration file alone does not establish
that an edition is fully translated.
