#pragma once

#include <QString>

// Export file names: the template and its tokens, Windows-safe names, and what
// happens when the name is already taken. Pure functions (export_tests).
namespace ExportNaming {

struct Tokens {
    QString name;      // source file stem
    QString film;      // stock display name; empty for no film
    QString date;      // yyyy-MM-dd
    QString camera;    // "Sony ILCE-7CR"; may be empty
    int sequence = 1;  // {seq}, written with 4 digits
};

enum class Collision { AddNumber, Replace, Skip };

struct Target {
    QString path;          // folder + fileName
    QString fileName;
    bool exists = false;   // a file already has the template's name
    bool skip = false;     // Skip rule (or no free number): nothing is written
};

QString expand(const QString& pattern, const Tokens& tokens);
QString sanitize(const QString& name);
QString extensionFor(const QString& format);
Collision collisionFromString(const QString& rule);
Target resolveTarget(const QString& folder, const QString& stem, const QString& extension, Collision rule);

}  // namespace ExportNaming
