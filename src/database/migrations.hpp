#pragma once

#include <cstddef>
#include <filesystem>

#include "database/postgres_connection.hpp"

namespace forge::database {

// Ensures a `schema_migrations` tracking table exists, then applies
// every "<digits>_*.sql" file under `migrations_dir` (sorted by that
// numeric prefix, so it doesn't depend on filesystem iteration order)
// whose version isn't already recorded there — each inside its own
// transaction (see Transaction), so a failing migration can never leave
// the schema half-applied. Returns how many were newly applied. Throws
// core::ForgeError if a filename doesn't start with digits, or if any
// migration fails.
std::size_t apply_pending_migrations(PostgresConnection& connection, const std::filesystem::path& migrations_dir);

} // namespace forge::database
