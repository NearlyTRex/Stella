//============================================================================
//
//   SSSS    tt          lll  lll
//  SS  SS   tt           ll   ll
//  SS     tttttt  eeee   ll   ll   aaaa
//   SSSS    tt   ee  ee  ll   ll      aa
//      SS   tt   eeeeee  ll   ll   aaaaa  --  "An Atari 2600 VCS Emulator"
//  SS  SS   tt   ee      ll   ll  aa  aa
//   SSSS     ttt  eeeee llll llll  aaaaa
//
// Copyright (c) 1995-2020 by Bradford W. Mott, Stephen Anthony
// and the Stella Team
//
// See the file "License.txt" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//============================================================================

#include <sstream>

#include "StellaDb.hxx"
#include "Logger.hxx"
#include "SqliteError.hxx"
#include "repository/KeyValueRepositoryNoop.hxx"
#include "repository/CompositeKeyValueRepositoryNoop.hxx"
#include "repository/CompositeKVRJsonAdapter.hxx"
#include "repository/KeyValueRepositoryConfigfile.hxx"
#include "repository/KeyValueRepositoryPropertyFile.hxx"
#include "KeyValueRepositorySqlite.hxx"
#include "SqliteStatement.hxx"
#include "FSNode.hxx"

namespace {
  constexpr Int32 CURRENT_VERSION = 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
StellaDb::StellaDb(const string& databaseDirectory, const string& databaseName)
  : myDatabaseDirectory{databaseDirectory},
    myDatabaseName{databaseName}
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void StellaDb::initialize()
{
  try {
    myDb = make_unique<SqliteDatabase>(myDatabaseDirectory, myDatabaseName);
    myDb->initialize();

    auto settingsRepository = make_unique<KeyValueRepositorySqlite>(*myDb, "settings", "setting", "value");
    settingsRepository->initialize();
    mySettingsRepository = std::move(settingsRepository);

    auto propertyRepositoryHost = make_unique<KeyValueRepositorySqlite>(*myDb, "properties", "md5", "properties");
    propertyRepositoryHost->initialize();
    myPropertyRepositoryHost = std::move(propertyRepositoryHost);

    myPropertyRepository = make_unique<CompositeKVRJsonAdapter>(*myPropertyRepositoryHost);

    if (myDb->getUserVersion() == 0) {
      initializeDb();
    } else {
      migrate();
    }
  }
  catch (const SqliteError& err) {
    Logger::error("sqlite DB " + databaseFileName() + " failed to initialize: " + err.what());

    mySettingsRepository = make_unique<KeyValueRepositoryNoop>();
    myPropertyRepository = make_unique<CompositeKeyValueRepositoryNoop>();

    myDb.reset();
    myPropertyRepositoryHost.reset();
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
const string StellaDb::databaseFileName() const
{
  return myDb ? FilesystemNode(myDb->fileName()).getShortPath() : "[failed]";
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void StellaDb::initializeDb() const
{
  FilesystemNode legacyConfigFile{myDatabaseDirectory};
  legacyConfigFile /= "stellarc";

  FilesystemNode legacyConfigDatabase{myDatabaseDirectory};
  legacyConfigDatabase /= "settings.sqlite3";

  FilesystemNode legacyPropertyFile{myDatabaseDirectory};
  legacyPropertyFile /= "stella.pro";

  if (legacyConfigDatabase.exists() && legacyConfigDatabase.isFile())
    importOldStellaDb(legacyConfigDatabase);
  else if (legacyConfigFile.exists() && legacyConfigFile.isFile())
    importStellarc(legacyConfigFile);

  if (legacyPropertyFile.exists() && legacyPropertyFile.isFile())
    importOldPropset(legacyPropertyFile);

  myDb->setUserVersion(CURRENT_VERSION);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void StellaDb::importStellarc(const FilesystemNode& node) const
{
  Logger::info("importing old settings from " + node.getPath());

    mySettingsRepository->save(KeyValueRepositoryConfigfile(node).load());
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void StellaDb::importOldStellaDb(const FilesystemNode& node) const {
  Logger::info("importing old settings from " + node.getPath());

  try {
    SqliteStatement(
      *myDb,
      "ATTACH DATABASE ? AS old_db"
    )
      .bind(1, node.getPath())
      .step();

    myDb->exec("INSERT INTO `settings` SELECT * FROM `old_db`.`settings`");
    myDb->exec("DETACH DATABASE `old_db`");
  }
  catch (const SqliteError& err) {
    Logger::error(err.what());
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void StellaDb::importOldPropset(const FilesystemNode& node) const
{
  Logger::info("importing old game properties from " + node.getPath());

  stringstream in;

  try {
    node.read(in);
  }
  catch (const runtime_error& err) {
    Logger::error(err.what());

    return;
  }
  catch (...) {
    Logger::error("import failed");

    return;
  }

  while (true) {
    auto props = KeyValueRepositoryPropertyFile::load(in);

    if (props.size() == 0) break;
    if ((props.find("Cart.MD5") == props.end()) || props["Cart.MD5"].toString() == "") continue;

    myPropertyRepository->get(props["Cart.MD5"].toString())->save(props);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void StellaDb::migrate() const
{
  Int32 version = myDb->getUserVersion();
  switch (version) {
    case 1:
      return;

    default: {
      stringstream ss;
      ss << "invalid database version " << version;

      throw new SqliteError(ss.str());
    }
  }
}