CREATE TABLE songs_new (

  rowid INTEGER PRIMARY KEY AUTOINCREMENT,

  title TEXT,
  titlesort TEXT,
  album TEXT,
  albumsort TEXT,
  artist TEXT,
  artistsort TEXT,
  albumartist TEXT,
  albumartistsort TEXT,
  track INTEGER NOT NULL DEFAULT -1,
  disc INTEGER NOT NULL DEFAULT -1,
  year INTEGER NOT NULL DEFAULT -1,
  originalyear INTEGER NOT NULL DEFAULT -1,
  genre TEXT,
  compilation INTEGER NOT NULL DEFAULT 0,
  composer TEXT,
  composersort TEXT,
  performer TEXT,
  performersort TEXT,
  grouping TEXT,
  comment TEXT,
  lyrics TEXT,
  synced_lyrics TEXT,

  artist_id TEXT,
  album_id TEXT,
  song_id TEXT,

  beginning INTEGER NOT NULL DEFAULT 0,
  length INTEGER NOT NULL DEFAULT 0,

  bitrate INTEGER NOT NULL DEFAULT -1,
  samplerate INTEGER NOT NULL DEFAULT -1,
  bitdepth INTEGER NOT NULL DEFAULT -1,

  source INTEGER NOT NULL DEFAULT 0,
  directory_id INTEGER NOT NULL DEFAULT -1,
  url TEXT NOT NULL,
  filetype INTEGER NOT NULL DEFAULT 0,
  filesize INTEGER NOT NULL DEFAULT -1,
  mtime INTEGER NOT NULL DEFAULT -1,
  ctime INTEGER NOT NULL DEFAULT -1,
  unavailable INTEGER DEFAULT 0,

  fingerprint TEXT,

  playcount INTEGER NOT NULL DEFAULT 0,
  skipcount INTEGER NOT NULL DEFAULT 0,
  lastplayed INTEGER NOT NULL DEFAULT -1,
  lastseen INTEGER NOT NULL DEFAULT -1,

  compilation_detected INTEGER DEFAULT 0,
  compilation_on INTEGER NOT NULL DEFAULT 0,
  compilation_off INTEGER NOT NULL DEFAULT 0,
  compilation_effective INTEGER NOT NULL DEFAULT 0,

  art_embedded INTEGER DEFAULT 0,
  art_automatic TEXT,
  art_automatic_mtime INTEGER NOT NULL DEFAULT 0,
  art_manual TEXT,
  art_unset INTEGER DEFAULT 0,

  effective_albumartist TEXT,
  effective_originalyear INTEGER NOT NULL DEFAULT 0,

  cue_path TEXT,

  rating INTEGER DEFAULT -1,

  acoustid_id TEXT,
  acoustid_fingerprint TEXT,

  musicbrainz_album_artist_id TEXT,
  musicbrainz_artist_id TEXT,
  musicbrainz_original_artist_id TEXT,
  musicbrainz_album_id TEXT,
  musicbrainz_original_album_id TEXT,
  musicbrainz_recording_id TEXT,
  musicbrainz_track_id TEXT,
  musicbrainz_disc_id TEXT,
  musicbrainz_release_group_id TEXT,
  musicbrainz_work_id TEXT,

  ebur128_integrated_loudness_lufs REAL,
  ebur128_loudness_range_lu REAL,

  bpm REAL,
  mood TEXT,
  initial_key TEXT,

  lyrics_storage_type INTEGER NOT NULL DEFAULT 0,
  synced_lyrics_storage_type INTEGER NOT NULL DEFAULT 0,
  lrc_mtime INTEGER NOT NULL DEFAULT 0

);

INSERT INTO songs_new (rowid, title, titlesort, album, albumsort, artist, artistsort, albumartist, albumartistsort, track, disc, year, originalyear, genre, compilation, composer, composersort, performer, performersort, grouping, comment, lyrics, synced_lyrics, artist_id, album_id, song_id, beginning, length, bitrate, samplerate, bitdepth, source, directory_id, url, filetype, filesize, mtime, ctime, unavailable, fingerprint, playcount, skipcount, lastplayed, lastseen, compilation_detected, compilation_on, compilation_off, compilation_effective, art_embedded, art_automatic, art_automatic_mtime, art_manual, art_unset, effective_albumartist, effective_originalyear, cue_path, rating, acoustid_id, acoustid_fingerprint, musicbrainz_album_artist_id, musicbrainz_artist_id, musicbrainz_original_artist_id, musicbrainz_album_id, musicbrainz_original_album_id, musicbrainz_recording_id, musicbrainz_track_id, musicbrainz_disc_id, musicbrainz_release_group_id, musicbrainz_work_id, ebur128_integrated_loudness_lufs, ebur128_loudness_range_lu, bpm, mood, initial_key, lyrics_storage_type, synced_lyrics_storage_type, lrc_mtime) SELECT ROWID, title, titlesort, album, albumsort, artist, artistsort, albumartist, albumartistsort, track, disc, year, originalyear, genre, compilation, composer, composersort, performer, performersort, grouping, comment, lyrics, synced_lyrics, artist_id, album_id, song_id, beginning, length, bitrate, samplerate, bitdepth, source, directory_id, url, filetype, filesize, mtime, ctime, unavailable, fingerprint, playcount, skipcount, lastplayed, lastseen, compilation_detected, compilation_on, compilation_off, compilation_effective, art_embedded, art_automatic, art_automatic_mtime, art_manual, art_unset, effective_albumartist, effective_originalyear, cue_path, rating, acoustid_id, acoustid_fingerprint, musicbrainz_album_artist_id, musicbrainz_artist_id, musicbrainz_original_artist_id, musicbrainz_album_id, musicbrainz_original_album_id, musicbrainz_recording_id, musicbrainz_track_id, musicbrainz_disc_id, musicbrainz_release_group_id, musicbrainz_work_id, ebur128_integrated_loudness_lufs, ebur128_loudness_range_lu, bpm, mood, initial_key, lyrics_storage_type, synced_lyrics_storage_type, lrc_mtime FROM songs ORDER BY ROWID;

DROP VIEW IF EXISTS duplicated_songs;

DROP TABLE songs;

ALTER TABLE songs_new RENAME TO songs;

CREATE UNIQUE INDEX IF NOT EXISTS idx_songs_url_beginning_cue_path ON songs (url, beginning, IFNULL(cue_path, ''));

CREATE INDEX IF NOT EXISTS idx_comp_artist ON songs (compilation_effective, artist);

CREATE INDEX IF NOT EXISTS idx_albumartist ON songs (albumartist);

CREATE INDEX IF NOT EXISTS idx_albumartistsort ON songs (albumartistsort);

CREATE INDEX IF NOT EXISTS idx_artist ON songs (artist);

CREATE INDEX IF NOT EXISTS idx_artistsort ON songs (artistsort);

CREATE INDEX IF NOT EXISTS idx_album ON songs (album);

CREATE INDEX IF NOT EXISTS idx_albumsort ON songs (albumsort);

CREATE INDEX IF NOT EXISTS idx_title ON songs (title);

CREATE INDEX IF NOT EXISTS idx_titlesort ON songs (titlesort);

CREATE INDEX IF NOT EXISTS idx_composersort ON songs (composersort);

CREATE INDEX IF NOT EXISTS idx_performersort ON songs (performersort);

CREATE VIEW IF NOT EXISTS duplicated_songs as select artist dup_artist, album dup_album, title dup_title from songs as inner_songs where artist != '' and album != '' and title != '' and unavailable = 0 group by artist, album , title having count(*) > 1;

INSERT INTO sqlite_sequence (name, seq) SELECT 'songs', 0 WHERE NOT EXISTS (SELECT 1 FROM sqlite_sequence WHERE name = 'songs');

UPDATE sqlite_sequence SET seq = MAX(seq, IFNULL((SELECT MAX(collection_id) FROM playlist_items), 0)) WHERE name = 'songs';

UPDATE schema_version SET version=31;
