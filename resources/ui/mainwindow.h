#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <TopoDS_Shape.hxx>

namespace Ui { class MainWindow; }
namespace forge::ui { class Viewport3D; }

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void setViewport(forge::ui::Viewport3D* viewport);
    void showBox(const TopoDS_Shape& shape);
private:
    Ui::MainWindow *ui;
    forge::ui::Viewport3D* viewport_ = nullptr;
};

#endif // MAINWINDOW_H
