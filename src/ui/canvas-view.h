#pragma once

#include <obs.hpp>
#include <graphics/matrix4.h>

#include <QWidget>

#include <vector>
#include <mutex>

enum class HandlePosition : uint32_t {
	None = 0,
	TopLeft = 1,
	TopRight = 2,
	BottomLeft = 3,
	BottomRight = 4,
};

class CanvasView : public QWidget {
	Q_OBJECT

public:
	CanvasView(QWidget *parent = nullptr);
	~CanvasView();

	obs_display_t *getDisplay() const { return display; }
	obs_sceneitem_t *getSelectedItem() const { return selectedItem; }
	void clearSelection() { selectedItem = nullptr; }
	void createDisplay();

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void moveEvent(QMoveEvent *event) override;
	QPaintEngine *paintEngine() const override { return nullptr; }

	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;

private:
	static void drawCallback(void *data, uint32_t cx, uint32_t cy);

	vec2 getMouseScenePos(QMouseEvent *event) const;
	obs_sceneitem_t *getItemAtPos(const vec2 &pos) const;
	HandlePosition getHandleAtPos(obs_sceneitem_t *item, const vec2 &pos) const;

	void getScaleAndOffset(int &x, int &y, float &scale) const;

	OBSDisplay display;
	bool destroying = false;

	OBSSceneItem selectedItem;
	HandlePosition activeHandle = HandlePosition::None;
	bool dragging = false;
	vec2 dragStartMouse;
	vec2 dragStartItemPos;
	vec2 dragStartItemScale;

	static constexpr float HANDLE_RADIUS = 8.0f;
};
