// Copyright (C) 2012, Chris J. Foster and the other authors and contributors
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name of the software's owners nor the names of its
//   contributors may be used to endorse or promote products derived from this
//   software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// (This is the BSD 3-clause license)

#include "mainwindow.h"
#include "ptview.h"

#include <QtCore/QDataStream>
#include <QtCore/QTimer>
#include <QtGui/QApplication>
#include <QtOpenGL/QGLFormat>

#include "argparse.h"
#include "config.h"
#include "displazserver.h"

class PointArray;
class TriMesh;
class LineSegments;


static QStringList g_initialFileNames;
static int storeFileName (int argc, const char *argv[])
{
    for(int i = 0; i < argc; ++i)
        g_initialFileNames.push_back (argv[i]);
    return 0;
}


/// Set up search paths to our application directory for Qt's file search
/// mechanism.
///
/// This allows us to use "shaders:points_default.glsl" as a path to a shader
/// in the rest of the code, regardless of the system-specific details of how
/// the install directories are laid out.
static void setupQFileSearchPaths()
{
    QString installBinDir = QCoreApplication::applicationDirPath();
    if (!installBinDir.endsWith("/bin"))
    {
        std::cerr << "WARNING: strange install location detected "
                     "- shaders will not be found\n";
        return;
    }
    QString installBaseDir = installBinDir;
    installBaseDir.chop(4);
    QDir::addSearchPath("shaders", installBaseDir + "/" + DISPLAZ_SHADER_DIR);
    QDir::addSearchPath("doc", installBaseDir + "/" + DISPLAZ_DOC_DIR);
}


int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    ArgParse::ArgParse ap;
    bool printVersion = false;
    bool printHelp = false;
    int maxPointCount = 200000000;
    std::string serverName;

    std::string shaderName;
    bool useServer = true;

    ap.options(
        "displaz - A lidar point cloud viewer\n"
        "Usage: displaz [opts] [file1.las ...]",
        "%*", storeFileName, "",
        "--maxpoints %d", &maxPointCount, "Maximum number of points to load at a time",
        "--noserver %!",  &useServer,     "Don't attempt to open files in existing window",
        "--server %s",    &serverName,    "Name of displaz instance to message on startup",
        "--shader %s",    &shaderName,    "Name of shader file to load on startup",
        "--version",      &printVersion,  "Print version number",
        "--help",         &printHelp,     "Print command line usage help",
        NULL
    );

    attachToParentConsole();
    if(ap.parse(argc, const_cast<const char**>(argv)) < 0)
    {
        ap.usage();
        std::cerr << "ERROR: " << ap.geterror() << std::endl;
        return EXIT_FAILURE;
    }

    if (printVersion)
    {
        std::cout << "version " DISPLAZ_VERSION_STRING "\n";
        return EXIT_SUCCESS;
    }
    if (printHelp)
    {
        ap.usage();
        return EXIT_SUCCESS;
    }

    setupQFileSearchPaths();

    qRegisterMetaType<std::shared_ptr<PointArray>>("std::shared_ptr<PointArray>");
    qRegisterMetaType<std::shared_ptr<TriMesh>>("std::shared_ptr<TriMesh>");
    qRegisterMetaType<std::shared_ptr<LineSegments>>("std::shared_ptr<LineSegments>");

    // TODO: Factor out this socket comms code - sending and recieving of
    // messages should happen in a centralised place.
    QString socketName = DisplazServer::socketName(QString::fromStdString(serverName));
    if (useServer)
    {
        QDir currentDir = QDir::current();
        QLocalSocket socket;
        // Attempt to locate a running displaz instance
        socket.connectToServer(socketName);
        if (socket.waitForConnected(100))
        {
            QByteArray command;
            if (g_initialFileNames.empty())
            {
                std::cerr << "WARNING: Existing window found, but no remote "
                             "command specified - exiting\n";
                // Since we opened the connection, close it nicely by sending a
                // zero-length message to say goodbye.
                command = "";
            }
            else
            {
                command = "OPEN_FILES";
                for (int i = 0; i < g_initialFileNames.size(); ++i)
                {
                    command += "\n";
                    command += currentDir.absoluteFilePath(g_initialFileNames[i]).toUtf8();
                }
            }
            QDataStream stream(&socket);
            // Writes length as big endian uint32 followed by raw bytes
            stream.writeBytes(command.data(), command.length());
            socket.disconnectFromServer();
            socket.waitForDisconnected(10000);
            //std::cerr << "Opening files in existing displaz instance\n";
            return EXIT_SUCCESS;
        }
    }
    // If we didn't find any other running instance, start up a server to
    // accept incoming connections, if desired
    std::unique_ptr<DisplazServer> server;
    if (useServer)
        server.reset(new DisplazServer(socketName));

    // Multisampled antialiasing - this makes rendered point clouds look much
    // nicer, but also makes the render much slower, especially on lower
    // powered graphics cards.
    //QGLFormat f = QGLFormat::defaultFormat();
    //f.setSampleBuffers(true);
    //QGLFormat::setDefaultFormat(f);

    PointViewerMainWindow window;
    if (useServer)
    {
        QObject::connect(server.get(), SIGNAL(messageReceived(QByteArray)),
                         &window, SLOT(runCommand(QByteArray)));
    }
    window.pointView().setMaxPointCount(maxPointCount);
    if (!shaderName.empty())
        window.openShaderFile(QString::fromStdString(shaderName));
    window.show();
    window.pointView().loadFiles(g_initialFileNames);

    return app.exec();
}
