#ifndef DISPLAZ_POINTINPUT_INCLUDED
#define DISPLAZ_POINTINPUT_INCLUDED

#include <memory>

#include <QFile>
#include <QObject>
#include <QStringList>

#include "geometry.h"
#include "qtlogger.h"

/// Loader for data files supported by displaz
///
/// This can be run in a separate thread from the main GUI thread to maintain
/// responsiveness when loading large point clouds.
///
class FileLoader : public QObject
{
    Q_OBJECT
    public:
        FileLoader(QStringList fileNames, size_t maxPointsPerFile,
                   bool removeAfterLoad, QObject* parent = 0)
            : QObject(parent),
            m_fileNames(fileNames),
            m_maxPointsPerFile(maxPointsPerFile),
            m_removeAfterLoad(removeAfterLoad)
        { }

    signals:
        /// Signal emitted when a load step starts
        void loadStepStarted(QString description);

        /// Emitted to report progress percent for current load step
        void loadProgress(int percent);

        /// Emit successfully loaded geometries
        void geometryLoaded(std::shared_ptr<Geometry> geom);

        /// Emitted when loading of all files is done
        void finished();

    public slots:
        /// Load all files provided to constructor
        void run()
        {
            for(int i = 0; i < m_fileNames.size(); ++i)
            {
                const QString& fileName = m_fileNames[i];
                std::shared_ptr<Geometry> geom = Geometry::create(fileName);
                connect(geom.get(), SIGNAL(loadProgress(int)),
                        this, SIGNAL(loadProgress(int)));
                connect(geom.get(), SIGNAL(loadStepStarted(QString)),
                        this, SIGNAL(loadStepStarted(QString)));
                try
                {
                    if (geom->loadFile(fileName, m_maxPointsPerFile))
                    {
                        emit geometryLoaded(geom);
                        if (m_removeAfterLoad)
                            QFile::remove(fileName);
                    }
                    else
                    {
                        g_logger.error("Could not load %s", fileName);
                    }
                }
                catch(std::bad_alloc& /*e*/)
                {
                    g_logger.error("Ran out of memory trying to load %s", fileName);
                }
                catch(std::exception& e)
                {
                    g_logger.error("Error loading %s: %s", fileName, e.what());
                }
            }
            emit finished();
        }

    private:
        QStringList m_fileNames;
        size_t m_maxPointsPerFile;
        bool m_removeAfterLoad;
};


#endif // DISPLAZ_POINTINPUT_INCLUDED
