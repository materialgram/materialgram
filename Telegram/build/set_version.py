import os

if os.path.isfile("version") is True:
    os.remove("version")

def nf(number, amount):
    n = str(number)
    while len(n) < amount:
        n += "0"
    return n

def replaceline(path, linecontains, replacewith):
    with open(path, "r") as file:
        lines = file.readlines()
    with open(path, "w") as file:
        for line in lines:
            if linecontains in line:
                file.write(f"{linecontains}{replacewith}\n")
            else:
                file.write(line)

major, minor, patch, materialver = input("Enter your major version: "), input("Minor: "), input("Patch: "), input("materialgram version: ")
ver = major + "." + minor + "." + patch + "." + materialver
print(ver)
with open("version", 'xt') as versionfile:
    versionfile.write(f"AppVersion         {nf(major, 3)}{nf(minor, 3)}{nf(patch, 2)}{materialver}\n"
                      f"AppVersionStrMajor {major}.{minor}\n"
                      f"AppVersionStrSmall {major}.{minor}.{patch}.{materialver}\n"
                      f"AppVersionStr      {major}.{minor}.{patch}.{materialver}\n"
                      f"BetaChannel        0\nAlphaVersion       0\n"
                      f"AppVersionOriginal {major}.{minor}.{patch}.{materialver}\n")
replaceline("../SourceFiles/core/version.h", 'constexpr auto AppVersionStr = "',
            f'{major}.{minor}.{patch}.{materialver}";')
replaceline("../SourceFiles/core/version.h", 'constexpr auto AppVersion = ',
            f'{nf(major, 3)}{nf(minor, 3)}{nf(patch, 2)}{materialver};')
replaceline("../Resources/winrc/Telegram.rc", ' FILEVERSION ',
            f'{major},{minor},{patch},{materialver}')
replaceline("../Resources/winrc/Telegram.rc", ' PRODUCTVERSION ',
            f'{major},{minor},{patch},{materialver}')
replaceline("../Resources/winrc/Updater.rc", ' FILEVERSION ',
            f'{major},{minor},{patch},{materialver}')
replaceline("../Resources/winrc/Updater.rc", ' PRODUCTVERSION ',
            f'{major},{minor},{patch},{materialver}')
replaceline("../Resources/winrc/Telegram.rc", '            VALUE "FileVersion", "',
            f'{major}.{minor}.{patch}.{materialver}"')
replaceline("../Resources/winrc/Updater.rc", '            VALUE "FileVersion", "',
            f'{major}.{minor}.{patch}.{materialver}"')
replaceline("../Resources/winrc/Telegram.rc", '            VALUE "ProductVersion", "',
            f'{major}.{minor}.{patch}.{materialver}"')
replaceline("../Resources/winrc/Updater.rc", '            VALUE "ProductVersion", "',
            f'{major}.{minor}.{patch}.{materialver}"')
