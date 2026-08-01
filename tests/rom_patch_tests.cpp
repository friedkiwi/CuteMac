#include <QCryptographicHash>

#include <iostream>

#include "cutemac/rom/RomPatcher.h"

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

} // namespace

int main()
{
    bool ok = true;
    const QByteArray original = QByteArray::fromHex("0011223344556677");
    const auto hash = QCryptographicHash::hash(original, QCryptographicHash::Sha256);
    const QVector<cutemac::rom::RomPatchDefinition> definitions {
        {
            QStringLiteral("test.patch"),
            QStringLiteral("Synthetic test patch"),
            QStringLiteral("test-machine"),
            hash,
            { { 2, QByteArray::fromHex("2233"), QByteArray::fromHex("aabb") } },
        },
    };

    auto patched = original;
    const auto result = cutemac::rom::RomPatcher::applyDefinitions(
        patched, QStringLiteral("test-machine"), { QStringLiteral("test.patch") }, definitions);
    ok &= expect(result.success, "matching patch must apply");
    ok &= expect(patched == QByteArray::fromHex("0011aabb44556677"), "replacement bytes are incorrect");
    ok &= expect(result.originalSha256 == hash, "original hash must be retained");

    auto unsupported = QByteArray::fromHex("1011223344556677");
    const auto unsupportedOriginal = unsupported;
    const auto unsupportedResult = cutemac::rom::RomPatcher::applyDefinitions(
        unsupported, QStringLiteral("test-machine"), { QStringLiteral("test.patch") }, definitions);
    ok &= expect(!unsupportedResult.success, "unsupported hash must fail");
    ok &= expect(unsupported == unsupportedOriginal, "failed patch must be transactional");

    auto wrongBytes = original;
    auto wrongDefinitions = definitions;
    wrongDefinitions[0].edits[0].expectedBytes = QByteArray::fromHex("ffff");
    const auto wrongResult = cutemac::rom::RomPatcher::applyDefinitions(
        wrongBytes, QStringLiteral("test-machine"), { QStringLiteral("test.patch") }, wrongDefinitions);
    ok &= expect(!wrongResult.success, "expected-byte mismatch must fail");
    ok &= expect(wrongBytes == original, "byte verification failure must not mutate the ROM");

    return ok ? 0 : 1;
}
