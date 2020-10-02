#include "pch.h"
#include "geometry_helpers.h"

using namespace DirectX;

mesh_data create_grid(float width, float height, UINT num_rows, UINT num_columns, std::string name)
{
    ASSERT2(num_rows > 1 && num_columns > 1, "A grid needs a least 1 row and 1 column");

    float dx = width / num_columns;
    float dy = height / num_rows;

    float half_width = 0.5f * width;
    float half_height = 0.5f * height;

    UINT row_vertices = num_rows + 1;
    UINT column_vertices = num_columns + 1;

    mesh_data mesh;
    mesh.name = name;
    mesh.vertices.resize(size_t(row_vertices * column_vertices));

    for (UINT i = 0; i < row_vertices; i++)
    {
        for (UINT j = 0; j < column_vertices; j++)
        {
            UINT index = i * column_vertices + j;
            mesh.vertices[index].position.x = -half_width + j * dx;
            mesh.vertices[index].position.y = 0.f;
            mesh.vertices[index].position.z = half_height - i * dy;
        }
    }
    UINT num_indices_per_row = (column_vertices * 2 * num_rows);
    UINT num_indices_per_column = (num_columns * 2 * row_vertices);
    mesh.indices.resize(num_indices_per_row + num_indices_per_column);

    UINT k = 0;
    for (UINT row = 0; row < row_vertices; row++)
    {
        // Skip the last column to break the line strip
        for (UINT column = 0; column < num_columns; column++)
        {
            UINT id = (column_vertices * row) + column;
            mesh.indices[k] = id;
            mesh.indices[k + 1] = id + 1;
            k = k + 2;
        }
    }

	// Skip the last row to break the line strip
    for (UINT row = 0; row < num_rows; row++)
    {
        for (UINT column = 0; column < column_vertices; column++)
        {
            mesh.indices[k] = (column_vertices * row ) + column;
            mesh.indices[k + 1] = (row + 1) * column_vertices + column;
            k = k + 2;
        }
    }

    return mesh;
}
