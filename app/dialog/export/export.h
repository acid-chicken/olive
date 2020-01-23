#ifndef EXPORTDIALOG_H
#define EXPORTDIALOG_H

#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QProgressBar>

#include "exportaudiotab.h"
#include "exportcodec.h"
#include "exportformat.h"
#include "exportvideotab.h"
#include "widget/viewer/viewer.h"

class ExportDialog : public QDialog
{
    Q_OBJECT
public:
    ExportDialog(ViewerOutput* viewer_node, QWidget* parent = nullptr);

public slots:
    virtual void accept() override;

protected:
    virtual void closeEvent(QCloseEvent *e) override;

private:
    void SetUpFormats();
    void LoadPresets();
    void SetDefaultFilename();

    QMatrix4x4 GenerateMatrix(ExportVideoTab::ScalingMethod method, int source_width, int source_height, int dest_width, int height);

    ViewerOutput* viewer_node_;

    QList<ExportFormat> formats_;
    int previously_selected_format_;

    QCheckBox* video_enabled_;
    QCheckBox* audio_enabled_;

    QList<ExportCodec> codecs_;

    ViewerWidget* preview_viewer_;
    QLineEdit* filename_edit_;
    QComboBox* format_combobox_;

    ExportVideoTab* video_tab_;
    ExportAudioTab* audio_tab_;

    double video_aspect_ratio_;

    ColorManager* color_manager_;

    QProgressBar* progress_bar_;

    enum Format {
        kFormatDNxHD,
        kFormatMatroska,
        kFormatMPEG4,
        kFormatOpenEXR,
        kFormatQuickTime,
        kFormatPNG,
        kFormatTIFF,

        kFormatCount
    };

    enum Codec {
        kCodecDNxHD,
        kCodecH264,
        kCodecH265,
        kCodecOpenEXR,
        kCodecPNG,
        kCodecProRes,
        kCodecTIFF,

        kCodecMP2,
        kCodecMP3,
        kCodecAAC,
        kCodecPCM,

        kCodecCount
    };

private slots:
    void BrowseFilename();

    void FormatChanged(int index);

    void ResolutionChanged();

    void VideoCodecChanged();

    void UpdateViewerDimensions();

    void ExporterIsDone();

};

#endif // EXPORTDIALOG_H
